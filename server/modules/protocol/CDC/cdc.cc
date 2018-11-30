/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2022-01-01
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

#include <cdc.hh>
#include <maxscale/alloc.h>
#include <maxscale/modinfo.h>
#include <maxscale/log.h>
#include <maxscale/protocol.h>
#include <maxscale/modinfo.h>
#include <maxscale/poll.h>

#define ISspace(x) isspace((int)(x))
#define CDC_SERVER_STRING "MaxScale(c) v.1.0.0"

static int           cdc_read_event(DCB* dcb);
static int           cdc_write_event(DCB* dcb);
static int           cdc_write(DCB* dcb, GWBUF* queue);
static int           cdc_error(DCB* dcb);
static int           cdc_hangup(DCB* dcb);
static int           cdc_accept(const SListener& listener);
static int           cdc_close(DCB* dcb);
static CDC_protocol* cdc_protocol_init(DCB* dcb);
static void          cdc_protocol_done(DCB* dcb);
static int           do_auth(DCB* dcb, GWBUF* buffer, void* data);
static void          write_auth_ack(DCB* dcb);
static void          write_auth_err(DCB* dcb);

static char* cdc_default_auth()
{
    return const_cast<char*>("CDCPlainAuth");
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
        static MXS_PROTOCOL MyObject =
        {
            cdc_read_event,     /* Read - EPOLLIN handler        */
            cdc_write,          /* Write - data from gateway     */
            cdc_write_event,    /* WriteReady - EPOLLOUT handler */
            cdc_error,          /* Error - EPOLLERR handler      */
            cdc_hangup,         /* HangUp - EPOLLHUP handler     */
            cdc_accept,         /* Accept                        */
            NULL,               /* Connect                       */
            cdc_close,          /* Close                         */
            NULL,               /* Authentication                */
            cdc_default_auth,   /* default authentication */
            NULL,
            NULL,
            NULL,
        };

        static MXS_MODULE info =
        {
            MXS_MODULE_API_PROTOCOL,
            MXS_MODULE_IN_DEVELOPMENT,
            MXS_PROTOCOL_VERSION,
            "A Change Data Capture Listener implementation for use in binlog events retrieval",
            "V1.0.0",
            MXS_NO_MODULE_CAPABILITIES,
            &MyObject,
            NULL,   /* Process init. */
            NULL,   /* Process finish. */
            NULL,   /* Thread init. */
            NULL,   /* Thread finish. */
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
 * @param dcb    The descriptor control block
 * @return
 */
static int cdc_read_event(DCB* dcb)
{
    MXS_SESSION* session = dcb->session;
    CDC_protocol* protocol = (CDC_protocol*) dcb->protocol;
    int n, rc = 0;
    GWBUF* head = NULL;
    int auth_val = CDC_STATE_AUTH_FAILED;
    CDC_session* client_data = (CDC_session*) dcb->data;

    if ((n = dcb_read(dcb, &head, 0)) > 0)
    {
        switch (protocol->state)
        {
        case CDC_STATE_WAIT_FOR_AUTH:
            /* Fill CDC_session from incoming packet */
            if (dcb->authfunc.extract(dcb, head))
            {
                /* Call protocol authentication */
                auth_val = dcb->authfunc.authenticate(dcb);
            }

            /* Discard input buffer */
            gwbuf_free(head);

            if (auth_val == CDC_STATE_AUTH_OK)
            {
                /* start a real session */
                session = session_alloc(dcb->service, dcb);

                if (session != NULL)
                {
                    protocol->state = CDC_STATE_HANDLE_REQUEST;

                    write_auth_ack(dcb);

                    MXS_INFO("%s: Client [%s] authenticated with user [%s]",
                             dcb->service->name,
                             dcb->remote != NULL ? dcb->remote : "",
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
                          dcb->service->name,
                          dcb->remote != NULL ? dcb->remote : "",
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
                         dcb->service->name,
                         dcb->remote != NULL ? dcb->remote : "");

                // gwbuf_set_type(head, GWBUF_TYPE_CDC);
                // the router will close the client connection
                // rc = MXS_SESSION_ROUTE_QUERY(session, head);

                // buffer not handled by router right now, consume it
                gwbuf_free(head);

                /* right now, just force the client connection close */
                dcb_close(dcb);
            }
            else
            {
                MXS_INFO("%s: Client [%s] requested [%.*s] action",
                         dcb->service->name,
                         dcb->remote != NULL ? dcb->remote : "",
                         (int)GWBUF_LENGTH(head),
                         (char*)GWBUF_DATA(head));

                // gwbuf_set_type(head, GWBUF_TYPE_CDC);
                rc = MXS_SESSION_ROUTE_QUERY(session, head);
            }
            break;

        default:
            MXS_INFO("%s: Client [%s] in unknown state %d",
                     dcb->service->name,
                     dcb->remote != NULL ? dcb->remote : "",
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

/**
 * Handler for the EPOLLIN event when the DCB refers to the listening
 * socket for the protocol.
 *
 * @param dcb    The descriptor control block
 */
static int cdc_accept(const SListener& listener)
{
    int n_connect = 0;
    DCB* client_dcb;

    while ((client_dcb = dcb_accept(listener)) != NULL)
    {
        CDC_session* client_data = NULL;
        CDC_protocol* protocol = NULL;

        /* allocating CDC protocol */
        protocol = cdc_protocol_init(client_dcb);
        if (protocol == NULL)
        {
            client_dcb->protocol = NULL;
            dcb_close(client_dcb);
            continue;
        }

        client_dcb->protocol = (CDC_protocol*) protocol;

        /* Dummy session */
        client_dcb->session = session_set_dummy(client_dcb);

        if (NULL == client_dcb->session || poll_add_dcb(client_dcb))
        {
            dcb_close(client_dcb);
            continue;
        }

        /*
         * create the session data for CDC
         * this coud be done in anothe routine, let's keep it here for now
         */
        client_data = (CDC_session*) MXS_CALLOC(1, sizeof(CDC_session));
        if (client_data == NULL)
        {
            dcb_close(client_dcb);
            continue;
        }

        client_dcb->data = client_data;

        /* client protocol state change to CDC_STATE_WAIT_FOR_AUTH */
        protocol->state = CDC_STATE_WAIT_FOR_AUTH;

        MXS_NOTICE("%s: new connection from [%s]",
                   client_dcb->service->name,
                   client_dcb->remote != NULL ? client_dcb->remote : "");

        n_connect++;
    }

    return n_connect;
}

/**
 * The close handler for the descriptor. Called by the gateway to
 * explicitly close a connection.
 *
 * @param dcb   The descriptor control block
 */
static int cdc_close(DCB* dcb)
{
    CDC_protocol* p = (CDC_protocol*) dcb->protocol;

    if (!p)
    {
        return 0;
    }

    /* Add deallocate protocol items*/
    cdc_protocol_done(dcb);

    return 1;
}

/**
 * Allocate a new CDC protocol structure
 *
 * @param  dcb    The DCB where protocol is added
 * @return        New allocated protocol or NULL on errors
 *
 */
static CDC_protocol* cdc_protocol_init(DCB* dcb)
{
    CDC_protocol* p;

    p = (CDC_protocol*) MXS_CALLOC(1, sizeof(CDC_protocol));

    if (p == NULL)
    {
        return NULL;
    }

    p->state = CDC_ALLOC;

    pthread_mutex_init(&p->lock, NULL);

    /* memory allocation here */
    p->state = CDC_STATE_WAIT_FOR_AUTH;

    return p;
}

/**
 * Free resources in CDC protocol
 *
 * @param dcb    DCB with allocateid protocol
 *
 */
static void cdc_protocol_done(DCB* dcb)
{
    CDC_protocol* p = (CDC_protocol*) dcb->protocol;

    if (!p)
    {
        return;
    }

    p = (CDC_protocol*) dcb->protocol;

    pthread_mutex_lock(&p->lock);

    /* deallocate memory here */

    p->state = CDC_STATE_CLOSE;

    pthread_mutex_unlock(&p->lock);
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
