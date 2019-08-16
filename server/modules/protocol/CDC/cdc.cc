/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2023-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * @file cdc.c - Change Data Capture Listener protocol module
 *
 * The change data capture protocol module is intended as a mechanism to allow connections
 * into maxscale for the purpose of accessing information within
 * the maxscale with a Change Data Capture API interface (supporting Avro right now)
 * databases.
 *
 * In the first instance it is intended to connect, authenticate and retieve data in the Avro format
 * as requested by compatible clients.
 *
 * @verbatim
 * Revision History
 * Date     Who         Description
 * 11/01/2016   Massimiliano Pinto  Initial implementation
 *
 * @endverbatim
 */

#define MXS_MODULE_NAME "CDC"

#include <maxscale/ccdefs.hh>
#include <cdc.hh>
#include <maxbase/alloc.h>
#include <maxscale/modinfo.hh>
#include <maxscale/protocol.hh>
#include <maxscale/modinfo.hh>
#include <maxscale/poll.hh>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <maxscale/authenticator2.hh>
#include <maxscale/dcb.hh>
#include <maxscale/buffer.hh>
#include <maxscale/service.hh>
#include <maxscale/session.hh>
#include <maxscale/poll.hh>
#include <maxbase/atomic.h>
#include <maxscale/protocol2.hh>

#define ISspace(x) isspace((int)(x))
#define CDC_SERVER_STRING "MaxScale(c) v.1.0.0"

static int                   cdc_read_event(DCB* dcb);
static int                   cdc_write_event(DCB* dcb);
static int                   cdc_write(DCB* dcb, GWBUF* queue);
static int                   cdc_error(DCB* dcb);
static int                   cdc_hangup(DCB* dcb);

static bool                  cdc_init_connection(DCB*);
static void                  cdc_finish_connection(DCB* dcb);
static CDC_protocol*         cdc_protocol_init();

static int                   do_auth(DCB* dcb, GWBUF* buffer, void* data);
static void                  write_auth_ack(DCB* dcb);
static void                  write_auth_err(DCB* dcb);

static char* cdc_default_auth()
{
    return const_cast<char*>("CDCPlainAuth");
}

MXS_PROTOCOL_SESSION* CDC_protocol::create(MXS_SESSION* session, mxs::Component* component)
{
    return cdc_protocol_init();
}

char* CDC_protocol::auth_default()
{
    return cdc_default_auth();
}

int32_t CDC_protocol::read(DCB* dcb)
{
    return cdc_read_event(dcb);
}

int32_t CDC_protocol::write(DCB* dcb, GWBUF* buffer)
{
    return cdc_write(dcb, buffer);
}

int32_t CDC_protocol::write_ready(DCB* dcb)
{
    return cdc_write_event(dcb);
}

int32_t CDC_protocol::error(DCB* dcb)
{
    return cdc_error(dcb);
}

int32_t CDC_protocol::hangup(DCB* dcb)
{
    return cdc_hangup(dcb);
}

bool CDC_protocol::init_connection(DCB* dcb)
{
    return cdc_init_connection(dcb);
}

void CDC_protocol::finish_connection(DCB* dcb)
{
    cdc_finish_connection(dcb);
}

GWBUF* CDC_protocol::reject(const char* host)
{
    return nullptr;
}

extern "C"
{
/**
 * The module entry point routine. It is this routine that
 * must populate the structure that is referred to as the
 * "module object", this is a structure with the set of
 * external entry points for this module.
 *
 * @return The module object
 */
MXS_MODULE* MXS_CREATE_MODULE()
{
    static MXS_MODULE info =
    {
        MXS_MODULE_API_PROTOCOL,
        MXS_MODULE_IN_DEVELOPMENT,
        MXS_PROTOCOL_VERSION,
        "A Change Data Capture Listener implementation for use in binlog events retrieval",
        "V1.0.0",
        MXS_NO_MODULE_CAPABILITIES,
        &mxs::ClientProtocolApi<CDC_protocol>::s_api,
        NULL,       /* Process init. */
        NULL,       /* Process finish. */
        NULL,       /* Thread init. */
        NULL,       /* Thread finish. */
        {
            {MXS_END_MODULE_PARAMS}
        }
    };

    return &info;
}
}

/**
 * Read event for EPOLLIN on the CDC protocol module.
 *
 * @param generic_dcb    The descriptor control block
 * @return
 */
static int cdc_read_event(DCB* generic_dcb)
{
    ClientDCB* dcb = static_cast<ClientDCB*>(generic_dcb);
    MXS_SESSION* session = dcb->session();
    CDC_protocol* protocol = (CDC_protocol*) dcb->protocol_session();
    int n, rc = 0;
    GWBUF* head = NULL;
    int auth_val = CDC_STATE_AUTH_FAILED;
    CDC_session* client_data = (CDC_session*) dcb->m_data;

    if ((n = dcb_read(dcb, &head, 0)) > 0)
    {
        switch (protocol->state)
        {
        case CDC_STATE_WAIT_FOR_AUTH:
            /* Fill CDC_session from incoming packet */
            if (dcb->m_auth_session->extract(dcb, head))
            {
                /* Call protocol authentication */
                auth_val = dcb->m_auth_session->authenticate(dcb);
            }

            /* Discard input buffer */
            gwbuf_free(head);

            if (auth_val == CDC_STATE_AUTH_OK)
            {
                if (session_start(dcb->session()))
                {
                    protocol->state = CDC_STATE_HANDLE_REQUEST;

                    write_auth_ack(dcb);

                    MXS_INFO("%s: Client [%s] authenticated with user [%s]",
                             dcb->service()->name(),
                             dcb->m_remote != NULL ? dcb->m_remote : "",
                             client_data->user);
                }
                else
                {
                    auth_val = CDC_STATE_AUTH_NO_SESSION;
                }
            }

            if (auth_val != CDC_STATE_AUTH_OK)
            {
                protocol->state = CDC_STATE_AUTH_ERR;

                write_auth_err(dcb);
                MXS_ERROR("%s: authentication failure from [%s], user [%s]",
                          dcb->service()->name(),
                          dcb->m_remote != NULL ? dcb->m_remote : "",
                          client_data->user);

                /* force the client connection close */
                dcb_close(dcb);
            }
            break;

        case CDC_STATE_HANDLE_REQUEST:
            // handle CLOSE command, it shoudl be routed as well and client connection closed after last
            // transmission
            if (strncmp((char*)GWBUF_DATA(head), "CLOSE", GWBUF_LENGTH(head)) == 0)
            {
                MXS_INFO("%s: Client [%s] has requested CLOSE action",
                         dcb->service()->name(),
                         dcb->m_remote != NULL ? dcb->m_remote : "");

                // gwbuf_set_type(head, GWBUF_TYPE_CDC);
                // the router will close the client connection
                // rc = mxs_route_query(session, head);

                // buffer not handled by router right now, consume it
                gwbuf_free(head);

                /* right now, just force the client connection close */
                dcb_close(dcb);
            }
            else
            {
                MXS_INFO("%s: Client [%s] requested [%.*s] action",
                         dcb->service()->name(),
                         dcb->m_remote != NULL ? dcb->m_remote : "",
                         (int)GWBUF_LENGTH(head),
                         (char*)GWBUF_DATA(head));

                // gwbuf_set_type(head, GWBUF_TYPE_CDC);
                rc = mxs_route_query(session, head);
            }
            break;

        default:
            MXS_INFO("%s: Client [%s] in unknown state %d",
                     dcb->service()->name(),
                     dcb->m_remote != NULL ? dcb->m_remote : "",
                     protocol->state);
            gwbuf_free(head);

            break;
        }
    }

    return rc;
}

/**
 * EPOLLOUT handler for the CDC protocol module.
 *
 * @param dcb    The descriptor control block
 * @return
 */
static int cdc_write_event(DCB* dcb)
{
    return dcb_drain_writeq(dcb);
}

/**
 * Write routine for the CDC protocol module.
 *
 * Writes the content of the buffer queue to the socket
 * observing the non-blocking principles of the gateway.
 *
 * @param dcb   Descriptor Control Block for the socket
 * @param queue Linked list of buffes to write
 */
static int cdc_write(DCB* dcb, GWBUF* queue)
{
    int rc;
    rc = dcb_write(dcb, queue);
    return rc;
}

/**
 * Handler for the EPOLLERR event.
 *
 * @param dcb    The descriptor control block
 */
static int cdc_error(DCB* dcb)
{
    dcb_close(dcb);
    return 0;
}

/**
 * Handler for the EPOLLHUP event.
 *
 * @param dcb    The descriptor control block
 */
static int cdc_hangup(DCB* dcb)
{
    dcb_close(dcb);
    return 0;
}

static bool cdc_init_connection(DCB* client_dcb)
{
    bool inited = false;

    CDC_protocol* protocol = static_cast<CDC_protocol*>(client_dcb->protocol_session());

    mxb_assert(client_dcb->session());

    /*
     * create the session data for CDC
     * this coud be done in anothe routine, let's keep it here for now
     */
    CDC_session* client_data = (CDC_session*) MXS_CALLOC(1, sizeof(CDC_session));
    if (client_data)
    {
        client_dcb->m_data = client_data;

        /* client protocol state change to CDC_STATE_WAIT_FOR_AUTH */
        protocol->state = CDC_STATE_WAIT_FOR_AUTH;

        MXS_NOTICE("%s: new connection from [%s]",
                   client_dcb->service()->name(),
                   client_dcb->m_remote != NULL ? client_dcb->m_remote : "");

        inited = true;
    }

    return inited;
}

static void cdc_finish_connection(DCB* client_dcb)
{
}

/**
 * Allocate a new CDC protocol structure
 *
 * @return        New allocated protocol or NULL on errors
 *
 */
static CDC_protocol* cdc_protocol_init()
{
    CDC_protocol* p = new (std::nothrow) CDC_protocol();
    if (p)
    {
        /* memory allocation here */
        p->state = CDC_STATE_WAIT_FOR_AUTH;
    }
    return p;
}

/**
 * Writes Authentication ACK, success
 *
 * @param dcb    Current client DCB
 *
 */
static void write_auth_ack(DCB* dcb)
{
    dcb_printf(dcb, "OK\n");
}

/**
 * Writes Authentication ERROR
 *
 * @param dcb    Current client DCB
 *
 */
static void write_auth_err(DCB* dcb)
{
    dcb_printf(dcb, "ERROR: Authentication failed\n");
}
