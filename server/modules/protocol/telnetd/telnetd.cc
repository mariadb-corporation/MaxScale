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

#define MXS_MODULE_NAME "telnetd"

#include <maxscale/ccdefs.hh>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <maxscale/alloc.h>
#include <maxscale/dcb.hh>
#include <maxscale/buffer.h>
#include <maxscale/service.hh>
#include <maxscale/session.hh>
#include <maxscale/protocol.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <maxscale/router.hh>
#include <maxscale/poll.hh>
#include <maxbase/atomic.h>
#include <telnetd.hh>
#include <maxscale/adminusers.h>
#include <maxscale/modinfo.h>

/**
 * @file telnetd.c - telnet daemon protocol module
 *
 * The telnetd protocol module is intended as a mechanism to allow connections
 * into the gateway for the purpsoe of accessing debugging information within
 * the gateway rather than a protocol to be used to send queries to backend
 * databases.
 *
 * In the first instance it is intended to allow a debug connection to access
 * internal data structures, however it may also be used to manage the
 * configuration of the gateway.
 *
 * @verbatim
 * Revision History
 * Date         Who                     Description
 * 17/06/2013   Mark Riddoch            Initial version
 * 17/07/2013   Mark Riddoch            Addition of login phase
 * 07/07/2015   Martin Brampton         Call unified dcb_close on error
 *
 * @endverbatim
 */

static int   telnetd_read_event(DCB* dcb);
static int   telnetd_write_event(DCB* dcb);
static int   telnetd_write(DCB* dcb, GWBUF* queue);
static int   telnetd_error(DCB* dcb);
static int   telnetd_hangup(DCB* dcb);
static int   telnetd_accept(DCB*);
static int   telnetd_close(DCB* dcb);
static char* telnetd_default_auth();

/**
 * The "module object" for the telnetd protocol module.
 */


static void telnetd_command(DCB*, unsigned char* cmd);
static void telnetd_echo(DCB* dcb, int enable);

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
        MXS_INFO("Initialise Telnetd Protocol module.");

        static MXS_PROTOCOL MyObject =
        {
            telnetd_read_event,         /**< Read - EPOLLIN handler        */
            telnetd_write,              /**< Write - data from gateway     */
            telnetd_write_event,        /**< WriteReady - EPOLLOUT handler */
            telnetd_error,              /**< Error - EPOLLERR handler      */
            telnetd_hangup,             /**< HangUp - EPOLLHUP handler     */
            telnetd_accept,             /**< Accept                        */
            NULL,                       /**< Connect                       */
            telnetd_close,              /**< Close                         */
            NULL,                       /**< Authentication                */
            telnetd_default_auth,       /**< Default authenticator         */
            NULL,                       /**< Connection limit reached      */
            NULL,
            NULL,
        };

        static MXS_MODULE info =
        {
            MXS_MODULE_API_PROTOCOL,
            MXS_MODULE_GA,
            MXS_PROTOCOL_VERSION,
            "A telnet deamon protocol for simple administration interface",
            "V1.1.1",
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
/*lint +e14 */

/**
 * The default authenticator name for this protocol
 *
 * @return name of authenticator
 */
static char* telnetd_default_auth()
{
    return const_cast<char*>("NullAuthAllow");
}

/**
 * Read event for EPOLLIN on the telnetd protocol module.
 *
 * @param dcb   The descriptor control block
 * @return
 */
static int telnetd_read_event(DCB* dcb)
{
    int n;
    GWBUF* head = NULL;
    MXS_SESSION* session = dcb->session;
    TELNETD* telnetd = (TELNETD*)dcb->protocol;
    char* password, * t;

    if ((n = dcb_read(dcb, &head, 0)) != -1)
    {
        if (head)
        {
            unsigned char* ptr = GWBUF_DATA(head);
            ptr = GWBUF_DATA(head);
            while (GWBUF_LENGTH(head) && *ptr == TELNET_IAC)
            {
                telnetd_command(dcb, ptr + 1);
                GWBUF_CONSUME(head, 3);
                ptr = GWBUF_DATA(head);
            }
            if (GWBUF_LENGTH(head))
            {
                switch (telnetd->state)
                {
                case TELNETD_STATE_LOGIN:
                    telnetd->username = strndup((char*)GWBUF_DATA(head), GWBUF_LENGTH(head));
                    /* Strip the cr/lf from the username */
                    t = strstr(telnetd->username, "\r\n");
                    if (t)
                    {
                        *t = 0;
                    }
                    telnetd->state = TELNETD_STATE_PASSWD;
                    dcb_printf(dcb, "Password: ");
                    telnetd_echo(dcb, 0);
                    gwbuf_consume(head, GWBUF_LENGTH(head));
                    break;

                case TELNETD_STATE_PASSWD:
                    password = strndup((char*)GWBUF_DATA(head), GWBUF_LENGTH(head));
                    /* Strip the cr/lf from the username */
                    t = strstr(password, "\r\n");
                    if (t)
                    {
                        *t = 0;
                    }
                    if (admin_verify_inet_user(telnetd->username, password))
                    {
                        telnetd_echo(dcb, 1);
                        telnetd->state = TELNETD_STATE_DATA;
                        dcb_printf(dcb, "\n\nMaxScale> ");
                    }
                    else
                    {
                        dcb_printf(dcb, "\n\rLogin incorrect\n\rLogin: ");
                        telnetd_echo(dcb, 1);
                        telnetd->state = TELNETD_STATE_LOGIN;
                        MXS_FREE(telnetd->username);
                    }
                    gwbuf_consume(head, GWBUF_LENGTH(head));
                    MXS_FREE(password);
                    break;

                case TELNETD_STATE_DATA:
                    MXS_SESSION_ROUTE_QUERY(session, head);
                    break;
                }
            }
            else
            {
                // Force the free of the buffer header
                gwbuf_consume(head, 0);
            }
        }
    }
    return n;
}

/**
 * EPOLLOUT handler for the telnetd protocol module.
 *
 * @param dcb   The descriptor control block
 * @return
 */
static int telnetd_write_event(DCB* dcb)
{
    return dcb_drain_writeq(dcb);
}

/**
 * Write routine for the telnetd protocol module.
 *
 * Writes the content of the buffer queue to the socket
 * observing the non-blocking principles of the gateway.
 *
 * @param dcb   Descriptor Control Block for the socket
 * @param queue Linked list of buffes to write
 */
static int telnetd_write(DCB* dcb, GWBUF* queue)
{
    int rc;
    rc = dcb_write(dcb, queue);
    return rc;
}

/**
 * Handler for the EPOLLERR event.
 *
 * @param dcb   The descriptor control block
 */
static int telnetd_error(DCB* dcb)
{
    return 0;
}

/**
 * Handler for the EPOLLHUP event.
 *
 * @param dcb   The descriptor control block
 */
static int telnetd_hangup(DCB* dcb)
{
    return 0;
}

/**
 * Handler for the EPOLLIN event when the DCB refers to the listening
 * socket for the protocol.
 *
 * @param listener   The descriptor control block
 * @return The number of new connections created
 */
static int telnetd_accept(DCB* client_dcb)
{
    TELNETD* telnetd_protocol = NULL;

    if ((telnetd_protocol = (TELNETD*)MXS_CALLOC(1, sizeof(TELNETD))) == NULL)
    {
        dcb_close(client_dcb);
        return 0;
    }
    telnetd_protocol->state = TELNETD_STATE_LOGIN;
    telnetd_protocol->username = NULL;
    client_dcb->protocol = (void*)telnetd_protocol;

    if (!session_start(client_dcb->session) || poll_add_dcb(client_dcb))
    {
        dcb_close(client_dcb);
        return 0;
    }

    ssl_authenticate_client(client_dcb, client_dcb->authfunc.connectssl(client_dcb));

    dcb_printf(client_dcb, "MaxScale login: ");

    return 1;
}

/**
 * The close handler for the descriptor. Called by the gateway to
 * explicitly close a connection.
 *
 * @param dcb   The descriptor control block
 */

static int telnetd_close(DCB* dcb)
{
    TELNETD* telnetd = static_cast<TELNETD*>(dcb->protocol);

    if (telnetd && telnetd->username)
    {
        MXS_FREE(telnetd->username);
    }

    return 0;
}

/**
 * Telnet command implementation
 *
 * Called for each command in the telnet stream.
 *
 * Currently we do no command execution
 *
 * @param       dcb     The client DCB
 * @param       cmd     The command stream
 */
static void telnetd_command(DCB* dcb, unsigned char* cmd)
{
}

/**
 * Enable or disable telnet protocol echo
 *
 * @param dcb           DCB of the telnet connection
 * @param enable        Enable or disable echo functionality
 */
static void telnetd_echo(DCB* dcb, int enable)
{
    GWBUF* gwbuf;
    unsigned char* buf;

    if ((gwbuf = gwbuf_alloc(3)) == NULL)
    {
        return;
    }
    buf = GWBUF_DATA(gwbuf);
    buf[0] = TELNET_IAC;
    buf[1] = enable ? TELNET_WONT : TELNET_WILL;
    buf[2] = TELNET_ECHO;
    dcb_write(dcb, gwbuf);
}
