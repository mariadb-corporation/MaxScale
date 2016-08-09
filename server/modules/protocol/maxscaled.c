/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl.
 *
 * Change Date: 2019-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <dcb.h>
#include <buffer.h>
#include <gw_protocol.h>
#include <service.h>
#include <session.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <router.h>
#include <maxscale/poll.h>
#include <atomic.h>
#include <gw.h>
#include <adminusers.h>
#include <skygw_utils.h>
#include <log_manager.h>
#include <modinfo.h>
#include <maxscaled.h>
#include <maxadmin.h>

 /* @see function load_module in load_utils.c for explanation of the following
  * lint directives.
 */
/*lint -e14 */
MODULE_INFO info =
{
    MODULE_API_PROTOCOL,
    MODULE_GA,
    GWPROTOCOL_VERSION,
    "A maxscale protocol for the administration interface"
};
/*lint +e14 */

/**
 * @file maxscaled.c - MaxScale administration protocol
 *
 *
 * @verbatim
 * Revision History
 * Date         Who                     Description
 * 13/06/2014   Mark Riddoch            Initial implementation
 * 07/07/2015   Martin Brampton         Correct failure handling
 * 17/05/2016   Massimiliano Pinto      Check for UNIX socket address
 *
 * @endverbatim
 */

static char *version_str = "V2.0.0";

static int maxscaled_read_event(DCB* dcb);
static int maxscaled_write_event(DCB *dcb);
static int maxscaled_write(DCB *dcb, GWBUF *queue);
static int maxscaled_error(DCB *dcb);
static int maxscaled_hangup(DCB *dcb);
static int maxscaled_accept(DCB *dcb);
static int maxscaled_close(DCB *dcb);
static int maxscaled_listen(DCB *dcb, char *config);
static char *mxsd_default_auth();

/**
 * The "module object" for the maxscaled protocol module.
 */
static GWPROTOCOL MyObject =
{
    maxscaled_read_event,           /**< Read - EPOLLIN handler        */
    maxscaled_write,                /**< Write - data from gateway     */
    maxscaled_write_event,          /**< WriteReady - EPOLLOUT handler */
    maxscaled_error,                /**< Error - EPOLLERR handler      */
    maxscaled_hangup,               /**< HangUp - EPOLLHUP handler     */
    maxscaled_accept,               /**< Accept                        */
    NULL,                           /**< Connect                       */
    maxscaled_close,                /**< Close                         */
    maxscaled_listen,               /**< Create a listener             */
    NULL,                           /**< Authentication                */
    NULL,                           /**< Session                       */
    mxsd_default_auth,              /**< Default authenticator         */
    NULL                            /**< Connection limit reached      */
};

/**
 * Implementation of the mandatory version entry point
 *
 * @return version string of the module
 *
 * @see function load_module in load_utils.c for explanation of the following
 * lint directives.
 */
/*lint -e14 */
char*  version()
{
    return version_str;
}

/**
 * The module initialisation routine, called when the module
 * is first loaded.
 */
void ModuleInit()
{
    MXS_INFO("Initialise MaxScaled Protocol module.");;
}

/**
 * The module entry point routine. It is this routine that
 * must populate the structure that is referred to as the
 * "module object", this is a structure with the set of
 * external entry points for this module.
 *
 * @return The module object
 */
GWPROTOCOL* GetModuleObject()
{
    return &MyObject;
}
/*lint +e14 */

/**
 * The default authenticator name for this protocol
 *
 * @return name of authenticator
 */
static char *mxsd_default_auth()
{
    return "MaxAdminAuth";
}

/**
 * Read event for EPOLLIN on the maxscaled protocol module.
 *
 * @param dcb   The descriptor control block
 * @return
 */
static int maxscaled_read_event(DCB* dcb)
{
    int n;
    GWBUF *head = NULL;
    MAXSCALED *maxscaled = (MAXSCALED *)dcb->protocol;

    if ((n = dcb_read(dcb, &head, 0)) != -1)
    {
        if (head)
        {
            if (GWBUF_LENGTH(head))
            {
                if (maxscaled->state == MAXSCALED_STATE_DATA)
                {
                    SESSION_ROUTE_QUERY(dcb->session, head);
                    dcb_printf(dcb, "OK");
                }
            }
            else
            {
                // Force the free of the buffer header
                gwbuf_free(head);
            }
        }
    }
    return n;
}

/**
 * EPOLLOUT handler for the maxscaled protocol module.
 *
 * @param dcb   The descriptor control block
 * @return
 */
static int maxscaled_write_event(DCB *dcb)
{
    return dcb_drain_writeq(dcb);
}

/**
 * Write routine for the maxscaled protocol module.
 *
 * Writes the content of the buffer queue to the socket
 * observing the non-blocking principles of MaxScale.
 *
 * @param dcb   Descriptor Control Block for the socket
 * @param queue Linked list of buffes to write
 */
static int maxscaled_write(DCB *dcb, GWBUF *queue)
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
static int maxscaled_error(DCB *dcb)
{
    return 0;
}

/**
 * Handler for the EPOLLHUP event.
 *
 * @param dcb   The descriptor control block
 */
static int maxscaled_hangup(DCB *dcb)
{
    dcb_close(dcb);
    return 0;
}

/**
 * Handler for the EPOLLIN event when the DCB refers to the listening
 * socket for the protocol.
 *
 * @param dcb   The descriptor control block
 * @return The number of new connections created
 */
static int maxscaled_accept(DCB *listener)
{
    int n_connect = 0;
    DCB *client_dcb;
    socklen_t len = sizeof(struct ucred);
    struct ucred ucred;

    while ((client_dcb = dcb_accept(listener, &MyObject)) != NULL)
    {
        MAXSCALED *maxscaled_protocol = NULL;

        if ((maxscaled_protocol = (MAXSCALED *)calloc(1, sizeof(MAXSCALED))) == NULL)
        {
            dcb_close(client_dcb);
            continue;
        }

        maxscaled_protocol->username = NULL;
        maxscaled_protocol->state = MAXSCALED_STATE_LOGIN;

        /* Get UNIX client credentials from socket*/
        if (getsockopt(client_dcb->fd, SOL_SOCKET, SO_PEERCRED, &ucred, &len) == -1)
        {
            MXS_ERROR("Failed to get UNIX socket credentials for 'MaxScale Admin'");
            dcb_close(client_dcb);
            continue;
        }
        else
        {
             struct passwd pw_entry;
             struct passwd *pw_tmp;
             char buf[MAXADMIN_GETPWUID_BUF_LEN];

             /* Fetch username from UID */
             if (!getpwuid_r(ucred.uid, &pw_entry, buf, sizeof(buf), &pw_tmp))
             {
                  GWBUF *username;

                  /* Set user in protocol */
                  maxscaled_protocol->username = strdup(pw_entry.pw_name);

                  username = gwbuf_alloc(strlen(maxscaled_protocol->username) + 1);

                  strcpy(GWBUF_DATA(username), maxscaled_protocol->username);

                  /* Authenticate the user */
                  if (client_dcb->authfunc.extract(client_dcb, username) == 0 &&
                      client_dcb->authfunc.authenticate(client_dcb) == 0)
                  {
                      dcb_printf(client_dcb, "OK----");
                      maxscaled_protocol->state = MAXSCALED_STATE_DATA;
                      client_dcb->user = strdup(maxscaled_protocol->username);
                  }
                  else
                  {
                      dcb_printf(client_dcb, "FAILED");
                  }
             }
             else
             {
                  MXS_ERROR("Failed to get UNIX user %ld details for 'MaxScale Admin'",
                            (unsigned long)ucred.uid);
                  dcb_close(client_dcb);
                  continue;
             }
        }

        spinlock_init(&maxscaled_protocol->lock);
        client_dcb->protocol = (void *)maxscaled_protocol;

        client_dcb->session = session_alloc(listener->session->service, client_dcb);

        if (NULL == client_dcb->session || poll_add_dcb(client_dcb))
        {
            dcb_close(client_dcb);
            continue;
        }
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

static int maxscaled_close(DCB *dcb)
{
    MAXSCALED *maxscaled = dcb->protocol;

    if (!maxscaled)
    {
        return 0;
    }

    spinlock_acquire(&maxscaled->lock);
    if (maxscaled->username)
    {
        free(maxscaled->username);
        maxscaled->username = NULL;
    }
    spinlock_release(&maxscaled->lock);

    return 0;
}

/**
 * Maxscale daemon listener entry point
 *
 * @param       listener        The Listener DCB
 * @param       config          Configuration (ip:port)
 * @return      0 on failure, 1 on success
 */
static int maxscaled_listen(DCB *listener, char *config)
{
    char *socket_path;

    /* check for default UNIX socket */
    if (strncmp(config, MAXADMIN_CONFIG_DEFAULT_SOCKET_TAG, MAXADMIN_CONFIG_DEFAULT_SOCKET_TAG_LEN) == 0)
    {
       socket_path = MAXADMIN_DEFAULT_SOCKET;
    }
    else
    {
       socket_path = config;
    }

    /* check for UNIX socket path*/
    if (strchr(socket_path,'/') == NULL)
    {
        MXS_ERROR("Failed to start listening on '%s' with 'MaxScale Admin' protocol,"
                  " only UNIX domain sockets are supported. Remove all 'port' and 'address'"
                  " parameters from this listener and add 'socket=default' to enable UNIX domain sockets.", socket_path);
        return -1;
    }
    else
    {
        return (dcb_listen(listener, socket_path, "MaxScale Admin") < 0) ? 0 : 1;
    }
}
