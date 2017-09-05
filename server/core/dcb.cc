/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2020-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * @file dcb.c  -  Descriptor Control Block generic functions
 *
 * Descriptor control blocks provide the key mechanism for the interface
 * with the non-blocking socket polling routines. The descriptor control
 * block is the user data that is handled by the epoll system and contains
 * the state data and pointers to other components that relate to the
 * use of a file descriptor.
 */
#include <maxscale/dcb.h>

#include <arpa/inet.h>
#include <errno.h>
#include <inttypes.h>
#include <netinet/tcp.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <time.h>

#include <maxscale/alloc.h>
#include <maxscale/atomic.h>
#include <maxscale/atomic.h>
#include <maxscale/hashtable.h>
#include <maxscale/hk_heartbeat.h>
#include <maxscale/limits.h>
#include <maxscale/listener.h>
#include <maxscale/log_manager.h>
#include <maxscale/platform.h>
#include <maxscale/poll.h>
#include <maxscale/router.h>
#include <maxscale/semaphore.hh>
#include <maxscale/server.h>
#include <maxscale/service.h>
#include <maxscale/spinlock.h>
#include <maxscale/utils.h>

#include "maxscale/modules.h"
#include "maxscale/queuemanager.h"
#include "maxscale/semaphore.hh"
#include "maxscale/session.h"
#include "maxscale/worker.hh"
#include "maxscale/workertask.hh"

using maxscale::Worker;
using maxscale::WorkerTask;
using maxscale::Semaphore;

//#define DCB_LOG_EVENT_HANDLING
#if defined(DCB_LOG_EVENT_HANDLING)
#define DCB_EH_NOTICE(s, p) MXS_NOTICE(s, p)
#else
#define DCB_EH_NOTICE(s, p)
#endif

namespace
{

static struct
{
    DCB dcb_initialized; /** A DCB with null values, used for initialization. */
    DCB** all_dcbs;      /** #workers sized array of pointers to DCBs where dcbs are listed. */
    bool check_timeouts; /** Should session timeouts be checked. */
} this_unit;

static thread_local struct
{
    long next_timeout_check; /** When to next check for idle sessions. */
    DCB* current_dcb;        /** The DCB currently being handled by event handlers. */
} this_thread;

}

void dcb_global_init()
{
    this_unit.dcb_initialized.dcb_chk_top = CHK_NUM_DCB;
    this_unit.dcb_initialized.fd = DCBFD_CLOSED;
    this_unit.dcb_initialized.state = DCB_STATE_ALLOC;
    this_unit.dcb_initialized.ssl_state = SSL_HANDSHAKE_UNKNOWN;
    this_unit.dcb_initialized.dcb_chk_tail = CHK_NUM_DCB;

    int nthreads = config_threadcount();

    if ((this_unit.all_dcbs = (DCB**)MXS_CALLOC(nthreads, sizeof(DCB*))) == NULL)
    {
        MXS_OOM();
        raise(SIGABRT);
    }
}

void dcb_finish()
{
    // TODO: Free all resources.
}

static void dcb_initialize(DCB *dcb);
static void dcb_final_free(DCB *dcb);
static void dcb_final_close(DCB *dcb);
static void dcb_call_callback(DCB *dcb, DCB_REASON reason);
static int  dcb_null_write(DCB *dcb, GWBUF *buf);
static int  dcb_null_auth(DCB *dcb, SERVER *server, MXS_SESSION *session, GWBUF *buf);
static inline DCB * dcb_find_in_list(DCB *dcb);
static void dcb_stop_polling_and_shutdown (DCB *dcb);
static bool dcb_maybe_add_persistent(DCB *);
static inline bool dcb_write_parameter_check(DCB *dcb, GWBUF *queue);
static int dcb_bytes_readable(DCB *dcb);
static int dcb_read_no_bytes_available(DCB *dcb, int nreadtotal);
static int dcb_create_SSL(DCB* dcb, SSL_LISTENER *ssl);
static int dcb_read_SSL(DCB *dcb, GWBUF **head);
static GWBUF *dcb_basic_read(DCB *dcb, int bytesavailable, int maxbytes, int nreadtotal, int *nsingleread);
static GWBUF *dcb_basic_read_SSL(DCB *dcb, int *nsingleread);
static void dcb_log_write_failure(DCB *dcb, GWBUF *queue, int eno);
static int gw_write(DCB *dcb, GWBUF *writeq, bool *stop_writing);
static int gw_write_SSL(DCB *dcb, GWBUF *writeq, bool *stop_writing);
static int dcb_log_errors_SSL (DCB *dcb, int ret);
static int dcb_accept_one_connection(DCB *listener, struct sockaddr *client_conn);
static int dcb_listen_create_socket_inet(const char *host, uint16_t port);
static int dcb_listen_create_socket_unix(const char *path);
static int dcb_set_socket_option(int sockfd, int level, int optname, void *optval, socklen_t optlen);
static void dcb_add_to_all_list(DCB *dcb);
static DCB *dcb_find_free();
static void dcb_remove_from_list(DCB *dcb);

static uint32_t dcb_poll_handler(MXS_POLL_DATA *data, int thread_id, uint32_t events);
static uint32_t dcb_process_poll_events(DCB *dcb, uint32_t ev);
static bool dcb_session_check(DCB *dcb, const char *);

uint64_t dcb_get_session_id(DCB *dcb)
{
    return (dcb && dcb->session) ? dcb->session->ses_id : 0;
}

/**
 * @brief Initialize a DCB
 *
 * This routine puts initial values into the fields of the DCB pointed to
 * by the parameter.
 *
 * Most fields can be initialized by the assignment of the static
 * initialized DCB. The exception is the bitmask.
 *
 * @param *dcb    Pointer to the DCB to be initialized
 */
static void
dcb_initialize(DCB *dcb)
{
    *dcb = this_unit.dcb_initialized;

    dcb->poll.handler = dcb_poll_handler;
}

/**
 * @brief Allocate or recycle a new DCB.
 *
 * This routine performs the generic initialisation on the DCB before returning
 * the newly allocated or recycled DCB.
 *
 * Most fields will be already initialized by the list manager, through the
 * call to list_find_free, passing the DCB initialization function.
 *
 * Remaining fields are set from the given parameters, and then the DCB is
 * flagged as ready for use.
 *
 * @param dcb_role_t    The role for the new DCB
 * @return An available DCB or NULL if none could be allocated.
 */
DCB *
dcb_alloc(dcb_role_t role, SERV_LISTENER *listener)
{
    DCB *newdcb;

    if ((newdcb = (DCB *)MXS_MALLOC(sizeof(*newdcb))) == NULL)
    {
        return NULL;
    }

    dcb_initialize(newdcb);
    newdcb->dcb_role = role;
    newdcb->listener = listener;
    newdcb->last_read = hkheartbeat;

    return newdcb;
}

/**
 * Provided only for consistency, simply calls dcb_close to guarantee
 * safe disposal of a DCB
 *
 * @param dcb   The DCB to free
 */
void
dcb_free(DCB *dcb)
{
    dcb_close(dcb);
}

/**
 * Free a DCB and remove it from the chain of all DCBs
 *
 * @param dcb The DCB to free
 */
static void
dcb_final_free(DCB *dcb)
{
    CHK_DCB(dcb);
    ss_info_dassert(dcb->state == DCB_STATE_DISCONNECTED ||
                    dcb->state == DCB_STATE_ALLOC,
                    "dcb not in DCB_STATE_DISCONNECTED not in DCB_STATE_ALLOC state.");

    if (dcb->session)
    {
        /*<
         * Terminate client session.
         */
        MXS_SESSION *local_session = dcb->session;
        dcb->session = NULL;
        CHK_SESSION(local_session);
        if (SESSION_STATE_DUMMY != local_session->state)
        {
            bool is_client_dcb = (DCB_ROLE_CLIENT_HANDLER == dcb->dcb_role ||
                                  DCB_ROLE_INTERNAL == dcb->dcb_role);

            session_put_ref(local_session);

            if (is_client_dcb)
            {
                /** The client DCB is only freed once all other DCBs that the session
                 * uses have been freed. This will guarantee that the authentication
                 * data will be usable for all DCBs even if the client DCB has already
                 * been closed. */
                return;
            }
        }
    }
    dcb_free_all_memory(dcb);
}

/**
 * Free the memory belonging to a DCB
 *
 * NB The DCB is fully detached from all links except perhaps the session
 * dcb_client link.
 *
 * @param dcb The DCB to free
 */
void
dcb_free_all_memory(DCB *dcb)
{
    DCB_CALLBACK *cb_dcb;

    if (dcb->protocol)
    {
        MXS_FREE(dcb->protocol);
    }
    if (dcb->data && dcb->authfunc.free)
    {
        dcb->authfunc.free(dcb);
        dcb->data = NULL;
    }
    if (dcb->authfunc.destroy)
    {
        dcb->authfunc.destroy(dcb->authenticator_data);
        dcb->authenticator_data = NULL;
    }
    if (dcb->protoname)
    {
        MXS_FREE(dcb->protoname);
    }
    if (dcb->remote)
    {
        MXS_FREE(dcb->remote);
    }
    if (dcb->user)
    {
        MXS_FREE(dcb->user);
    }

    /* Clear write and read buffers */
    if (dcb->delayq)
    {
        gwbuf_free(dcb->delayq);
        dcb->delayq = NULL;
    }
    if (dcb->writeq)
    {
        gwbuf_free(dcb->writeq);
        dcb->writeq = NULL;
    }
    if (dcb->dcb_readqueue)
    {
        gwbuf_free(dcb->dcb_readqueue);
        dcb->dcb_readqueue = NULL;
    }
    if (dcb->dcb_fakequeue)
    {
        gwbuf_free(dcb->dcb_fakequeue);
        dcb->dcb_fakequeue = NULL;
    }

    while ((cb_dcb = dcb->callbacks) != NULL)
    {
        dcb->callbacks = cb_dcb->next;
        MXS_FREE(cb_dcb);
    }

    if (dcb->ssl)
    {
        SSL_free(dcb->ssl);
    }

    // Ensure that id is immediately the wrong one.
    dcb->poll.thread.id = 0xdeadbeef;
    MXS_FREE(dcb);

}

/**
 * Remove a DCB from the poll list and trigger shutdown mechanisms.
 *
 * @param       dcb     The DCB to be processed
 */
static void
dcb_stop_polling_and_shutdown(DCB *dcb)
{
    poll_remove_dcb(dcb);
    /**
     * close protocol and router session
     */
    if (dcb->func.close != NULL)
    {
        dcb->func.close(dcb);
    }
}

/**
 * Connect to a server
 *
 * This routine will create a server connection
 * If successful the new dcb will be put in
 * epoll set by dcb->func.connect
 *
 * @param server        The server to connect to
 * @param session       The session this connection is being made for
 * @param protocol      The protocol module to use
 * @return              The new allocated dcb or NULL if the DCB was not connected
 */
DCB *
dcb_connect(SERVER *server, MXS_SESSION *session, const char *protocol)
{
    DCB         *dcb;
    MXS_PROTOCOL  *funcs;
    int         fd;
    int         rc;
    const char  *user;

    user = session_get_user(session);
    if (user && strlen(user))
    {
        MXS_DEBUG("Looking for persistent connection DCB user %s protocol %s", user, protocol);
        dcb = server_get_persistent(server, user, protocol, session->client_dcb->poll.thread.id);
        if (dcb)
        {
            /**
             * Link dcb to session. Unlink is called in dcb_final_free
             */
            if (!session_link_dcb(session, dcb))
            {
                dcb_close(dcb);
                return NULL;
            }
            MXS_DEBUG("Reusing a persistent connection, dcb %p", dcb);
            dcb->persistentstart = 0;
            dcb->was_persistent = true;
            dcb->last_read = hkheartbeat;
            return dcb;
        }
        else
        {
            MXS_DEBUG("Failed to find a reusable persistent connection");
        }
    }

    if ((dcb = dcb_alloc(DCB_ROLE_BACKEND_HANDLER, NULL)) == NULL)
    {
        return NULL;
    }

    if ((funcs = (MXS_PROTOCOL *)load_module(protocol,
                                             MODULE_PROTOCOL)) == NULL)
    {
        dcb->state = DCB_STATE_DISCONNECTED;
        dcb_final_free(dcb);
        MXS_ERROR("Failed to load protocol module '%s'", protocol);
        return NULL;
    }
    memcpy(&(dcb->func), funcs, sizeof(MXS_PROTOCOL));
    dcb->protoname = MXS_STRDUP_A(protocol);

    const char *authenticator = server->authenticator ?
                                server->authenticator : dcb->func.auth_default ?
                                dcb->func.auth_default() : "NullAuthDeny";

    MXS_AUTHENTICATOR *authfuncs = (MXS_AUTHENTICATOR*)load_module(authenticator,
                                                                   MODULE_AUTHENTICATOR);
    if (authfuncs == NULL)
    {

        MXS_ERROR("Failed to load authenticator module '%s'", authenticator);
        dcb_close(dcb);
        return NULL;
    }

    memcpy(&dcb->authfunc, authfuncs, sizeof(MXS_AUTHENTICATOR));

    /**
     * Link dcb to session. Unlink is called in dcb_final_free
     */
    if (!session_link_dcb(session, dcb))
    {
        dcb_final_free(dcb);
        return NULL;
    }
    fd = dcb->func.connect(dcb, server, session);

    if (fd == DCBFD_CLOSED)
    {
        MXS_DEBUG("Failed to connect to server [%s]:%d, from backend dcb %p, client dcp %p fd %d",
                  server->name, server->port, dcb, session->client_dcb, session->client_dcb->fd);
        dcb->state = DCB_STATE_DISCONNECTED;
        dcb_final_free(dcb);
        return NULL;
    }
    else
    {
        MXS_DEBUG("Connected to server [%s]:%d, from backend dcb %p, client dcp %p fd %d.",
                  server->name, server->port, dcb, session->client_dcb, session->client_dcb->fd);
    }
    /**
     * Successfully connected to backend. Assign file descriptor to dcb
     */
    dcb->fd = fd;

    /**
     * Add server pointer to dcb
     */
    dcb->server = server;

    dcb->was_persistent = false;

    /**
     * backend_dcb is connected to backend server, and once backend_dcb
     * is added to poll set, authentication takes place as part of
     * EPOLLOUT event that will be received once the connection
     * is established.
     */

    /** Allocate DCB specific authentication data */
    if (dcb->authfunc.create &&
        (dcb->authenticator_data = dcb->authfunc.create(dcb->server->auth_instance)) == NULL)
    {
        MXS_ERROR("Failed to create authenticator for backend DCB.");
        dcb->state = DCB_STATE_DISCONNECTED;
        dcb_final_free(dcb);
        return NULL;
    }

    /**
     * Add the dcb in the poll set
     */
    rc = poll_add_dcb(dcb);

    if (rc)
    {
        dcb->state = DCB_STATE_DISCONNECTED;
        dcb_final_free(dcb);
        return NULL;
    }
    /**
     * The dcb will be addded into poll set by dcb->func.connect
     */
    atomic_add(&server->stats.n_connections, 1);
    atomic_add(&server->stats.n_current, 1);

    return dcb;
}

/**
 * General purpose read routine to read data from a socket in the
 * Descriptor Control Block and append it to a linked list of buffers.
 * The list may be empty, in which case *head == NULL. The third
 * parameter indicates the maximum number of bytes to be read (needed
 * for SSL processing) with 0 meaning no limit.
 *
 * @param dcb       The DCB to read from
 * @param head      Pointer to linked list to append data to
 * @param maxbytes  Maximum bytes to read (0 = no limit)
 * @return          -1 on error, otherwise the total number of bytes read
 */
int dcb_read(DCB   *dcb,
             GWBUF **head,
             int maxbytes)
{
    int     nsingleread = 0;
    int     nreadtotal = 0;

    if (dcb->dcb_readqueue)
    {
        *head = gwbuf_append(*head, dcb->dcb_readqueue);
        dcb->dcb_readqueue = NULL;
        nreadtotal = gwbuf_length(*head);
    }
    else if (dcb->dcb_fakequeue)
    {
        *head = gwbuf_append(*head, dcb->dcb_fakequeue);
        dcb->dcb_fakequeue = NULL;
        nreadtotal = gwbuf_length(*head);
    }

    if (SSL_HANDSHAKE_DONE == dcb->ssl_state || SSL_ESTABLISHED == dcb->ssl_state)
    {
        return dcb_read_SSL(dcb, head);
    }

    CHK_DCB(dcb);

    if (dcb->fd <= 0)
    {
        MXS_ERROR("Read failed, dcb is closed.");
        return 0;
    }

    while (0 == maxbytes || nreadtotal < maxbytes)
    {
        int bytes_available;

        bytes_available = dcb_bytes_readable(dcb);
        if (bytes_available <= 0)
        {
            return bytes_available < 0 ? -1 :
                   /** Handle closed client socket */
                   dcb_read_no_bytes_available(dcb, nreadtotal);
        }
        else
        {
            GWBUF *buffer;
            dcb->last_read = hkheartbeat;

            buffer = dcb_basic_read(dcb, bytes_available, maxbytes, nreadtotal, &nsingleread);
            if (buffer)
            {
                nreadtotal += nsingleread;
                MXS_DEBUG("Read %d bytes from dcb %p in state %s fd %d.",
                          nsingleread, dcb, STRDCBSTATE(dcb->state), dcb->fd);

                /*< Assign the target server for the gwbuf */
                buffer->server = dcb->server;
                /*< Append read data to the gwbuf */
                *head = gwbuf_append(*head, buffer);
            }
            else
            {
                break;
            }
        }
    } /*< while (0 == maxbytes || nreadtotal < maxbytes) */

    return nreadtotal;
}

/**
 * Find the number of bytes available for the DCB's socket
 *
 * @param dcb       The DCB to read from
 * @return          -1 on error, otherwise the total number of bytes available
 */
static int
dcb_bytes_readable(DCB *dcb)
{
    int bytesavailable;

    if (-1 == ioctl(dcb->fd, FIONREAD, &bytesavailable))
    {
        MXS_ERROR("ioctl FIONREAD for dcb %p in state %s fd %d failed: %d, %s",
                  dcb, STRDCBSTATE(dcb->state), dcb->fd, errno, mxs_strerror(errno));
        return -1;
    }
    else
    {
        return bytesavailable;
    }
}

/**
 * Determine the return code needed when read has run out of data
 *
 * @param dcb           The DCB to read from
 * @param nreadtotal    Number of bytes that have been read
 * @return              -1 on error, 0 for conditions not treated as error
 */
static int
dcb_read_no_bytes_available(DCB *dcb, int nreadtotal)
{
    /** Handle closed client socket */
    if (nreadtotal == 0 && DCB_ROLE_CLIENT_HANDLER == dcb->dcb_role)
    {
        char c;
        int l_errno = 0;
        long r = -1;

        /* try to read 1 byte, without consuming the socket buffer */
        r = recv(dcb->fd, &c, sizeof(char), MSG_PEEK);
        l_errno = errno;

        if (r <= 0 &&
            l_errno != EAGAIN &&
            l_errno != EWOULDBLOCK &&
            l_errno != 0)
        {
            return -1;
        }
    }
    return nreadtotal;
}

/**
 * Basic read function to carry out a single read operation on the DCB socket.
 *
 * @param dcb               The DCB to read from
 * @param bytesavailable    Pointer to linked list to append data to
 * @param maxbytes          Maximum bytes to read (0 = no limit)
 * @param nreadtotal        Total number of bytes already read
 * @param nsingleread       To be set as the number of bytes read this time
 * @return                  GWBUF* buffer containing new data, or null.
 */
static GWBUF *
dcb_basic_read(DCB *dcb, int bytesavailable, int maxbytes, int nreadtotal, int *nsingleread)
{
    GWBUF *buffer;

    int bufsize = MXS_MIN(bytesavailable, MXS_MAX_NW_READ_BUFFER_SIZE);
    if (maxbytes)
    {
        bufsize = MXS_MIN(bufsize, maxbytes - nreadtotal);
    }

    if ((buffer = gwbuf_alloc(bufsize)) == NULL)
    {
        *nsingleread = -1;
    }
    else
    {
        *nsingleread = read(dcb->fd, GWBUF_DATA(buffer), bufsize);
        dcb->stats.n_reads++;

        if (*nsingleread <= 0)
        {
            if (errno != 0 && errno != EAGAIN && errno != EWOULDBLOCK)
            {
                MXS_ERROR("Read failed, dcb %p in state %s fd %d: %d, %s",
                          dcb, STRDCBSTATE(dcb->state), dcb->fd,
                          errno, mxs_strerror(errno));
            }
            gwbuf_free(buffer);
            buffer = NULL;
        }
    }
    return buffer;
}

/**
 * General purpose read routine to read data from a socket through the SSL
 * structure lined with this DCB and append it to a linked list of buffers.
 * The list may be empty, in which case *head == NULL. The SSL structure should
 * be initialized and the SSL handshake should be done.
 *
 * @param dcb   The DCB to read from
 * @param head  Pointer to linked list to append data to
 * @return      -1 on error, otherwise the total number of bytes read
 */
static int
dcb_read_SSL(DCB *dcb, GWBUF **head)
{
    GWBUF *buffer;
    int nsingleread = 0, nreadtotal = 0;
    int start_length = gwbuf_length(*head);

    CHK_DCB(dcb);

    if (dcb->fd <= 0)
    {
        MXS_ERROR("Read failed, dcb is closed.");
        return -1;
    }

    if (dcb->ssl_write_want_read)
    {
        dcb_drain_writeq(dcb);
    }

    dcb->last_read = hkheartbeat;
    buffer = dcb_basic_read_SSL(dcb, &nsingleread);
    if (buffer)
    {
        nreadtotal += nsingleread;
        *head = gwbuf_append(*head, buffer);

        while (buffer)
        {
            dcb->last_read = hkheartbeat;
            buffer = dcb_basic_read_SSL(dcb, &nsingleread);
            if (buffer)
            {
                nreadtotal += nsingleread;
                /*< Append read data to the gwbuf */
                *head = gwbuf_append(*head, buffer);
            }
        }
    }

    ss_dassert(gwbuf_length(*head) == (size_t)(start_length + nreadtotal));

    return nsingleread < 0 ? nsingleread : nreadtotal;
}

/**
 * Basic read function to carry out a single read on the DCB's SSL connection
 *
 * @param dcb           The DCB to read from
 * @param nsingleread   To be set as the number of bytes read this time
 * @return              GWBUF* buffer containing the data, or null.
 */
static GWBUF *
dcb_basic_read_SSL(DCB *dcb, int *nsingleread)
{
    unsigned char temp_buffer[MXS_MAX_NW_READ_BUFFER_SIZE];
    GWBUF *buffer = NULL;

    *nsingleread = SSL_read(dcb->ssl, (void *)temp_buffer, MXS_MAX_NW_READ_BUFFER_SIZE);
    dcb->stats.n_reads++;

    switch (SSL_get_error(dcb->ssl, *nsingleread))
    {
    case SSL_ERROR_NONE:
        /* Successful read */
        MXS_DEBUG("Read %d bytes from dcb %p in state %s fd %d.",
                  *nsingleread, dcb, STRDCBSTATE(dcb->state), dcb->fd);
        if (*nsingleread && (buffer = gwbuf_alloc_and_load(*nsingleread, (void *)temp_buffer)) == NULL)
        {
            *nsingleread = -1;
            return NULL;
        }

        /* If we were in a retry situation, need to clear flag and attempt write */
        if (dcb->ssl_read_want_write || dcb->ssl_read_want_read)
        {
            dcb->ssl_read_want_write = false;
            dcb->ssl_read_want_read = false;
            dcb_drain_writeq(dcb);
        }
        break;

    case SSL_ERROR_ZERO_RETURN:
        /* react to the SSL connection being closed */
        MXS_DEBUG("SSL connection appears to have hung up");
        poll_fake_hangup_event(dcb);
        *nsingleread = 0;
        break;

    case SSL_ERROR_WANT_READ:
        /* Prevent SSL I/O on connection until retried, return to poll loop */
        MXS_DEBUG("SSL connection want read");
        dcb->ssl_read_want_write = false;
        dcb->ssl_read_want_read = true;
        *nsingleread = 0;
        break;

    case SSL_ERROR_WANT_WRITE:
        /* Prevent SSL I/O on connection until retried, return to poll loop */
        MXS_DEBUG("SSL connection want write");
        dcb->ssl_read_want_write = true;
        dcb->ssl_read_want_read = false;
        *nsingleread = 0;
        break;

    case SSL_ERROR_SYSCALL:
        *nsingleread = dcb_log_errors_SSL(dcb, *nsingleread);
        break;

    default:
        *nsingleread = dcb_log_errors_SSL(dcb, *nsingleread);
        break;
    }
    return buffer;
}

/**
 * Log errors from an SSL operation
 *
 * @param dcb       The DCB of the client
 * @param ret       Return code from SSL operation if error is SSL_ERROR_SYSCALL
 * @return          -1 if an error found, 0 if no error found
 */
static int
dcb_log_errors_SSL (DCB *dcb, int ret)
{
    char errbuf[MXS_STRERROR_BUFLEN];
    unsigned long ssl_errno;

    ssl_errno = ERR_get_error();
    if (0 == ssl_errno)
    {
        return 0;
    }
    if (ret || ssl_errno)
    {
        MXS_ERROR("SSL operation failed, dcb %p in state "
                  "%s fd %d return code %d. More details may follow.",
                  dcb, STRDCBSTATE(dcb->state), dcb->fd, ret);
    }
    if (ret && !ssl_errno)
    {
        int local_errno = errno;
        MXS_ERROR("SSL error caused by TCP error %d %s",
                  local_errno, mxs_strerror(local_errno));
    }
    else
    {
        while (ssl_errno != 0)
        {
            ERR_error_string_n(ssl_errno, errbuf, MXS_STRERROR_BUFLEN);
            MXS_ERROR("%s", errbuf);
            ssl_errno = ERR_get_error();
        }
    }
    return -1;
}

/**
 * General purpose routine to write to a DCB
 *
 * @param dcb   The DCB of the client
 * @param queue Queue of buffers to write
 * @return      0 on failure, 1 on success
 */
int
dcb_write(DCB *dcb, GWBUF *queue)
{
    // The following guarantees that queue is not NULL
    if (!dcb_write_parameter_check(dcb, queue))
    {
        return 0;
    }

    dcb->writeq = gwbuf_append(dcb->writeq, queue);
    dcb->stats.n_buffered++;
    dcb_drain_writeq(dcb);

    return 1;
}

/**
 * Check the parameters for dcb_write
 *
 * @param dcb   The DCB of the client
 * @param queue Queue of buffers to write
 * @return true if parameters acceptable, false otherwise
 */
static inline bool
dcb_write_parameter_check(DCB *dcb, GWBUF *queue)
{
    if (queue == NULL)
    {
        return false;
    }

    if (dcb->fd <= 0)
    {
        MXS_ERROR("Write failed, dcb is closed.");
        gwbuf_free(queue);
        return false;
    }

    if (dcb->session == NULL || dcb->session->state != SESSION_STATE_STOPPING)
    {
        /**
         * SESSION_STATE_STOPPING means that one of the backends is closing
         * the router session. Some backends may have not completed
         * authentication yet and thus they have no information about router
         * being closed. Session state is changed to SESSION_STATE_STOPPING
         * before router's closeSession is called and that tells that DCB may
         * still be writable.
         */
        if (dcb->state != DCB_STATE_ALLOC &&
            dcb->state != DCB_STATE_POLLING &&
            dcb->state != DCB_STATE_LISTENING &&
            dcb->state != DCB_STATE_NOPOLLING)
        {
            MXS_DEBUG("Write aborted to dcb %p because it is in state %s",
                      dcb, STRDCBSTATE(dcb->state));
            gwbuf_free(queue);
            return false;
        }
    }
    return true;
}

/**
 * Debug log write failure, except when it is COM_QUIT
 *
 * @param dcb   The DCB of the client
 * @param queue Queue of buffers to write
 * @param eno   Error number for logging
 */
static void
dcb_log_write_failure(DCB *dcb, GWBUF *queue, int eno)
{
    if (eno != EPIPE &&
        eno != EAGAIN &&
        eno != EWOULDBLOCK)
    {
        MXS_ERROR("Write to dcb %p in state %s fd %d failed: %d, %s",
                  dcb, STRDCBSTATE(dcb->state), dcb->fd, eno, mxs_strerror(eno));

    }
}

/**
 * @brief Drain the write queue of a DCB
 *
 * This is called as part of the EPOLLOUT handling of a socket and will try to
 * send any buffered data from the write queue up until the point the write would block.
 *
 * @param dcb DCB to drain
 * @return The number of bytes written
 */
int dcb_drain_writeq(DCB *dcb)
{
    if (dcb->ssl_read_want_write)
    {
        /** The SSL library needs to write more data */
        poll_fake_read_event(dcb);
    }

    int total_written = 0;
    GWBUF *local_writeq = dcb->writeq;
    dcb->writeq = NULL;

    while (local_writeq)
    {
        int written;
        bool stop_writing = false;
        /* The value put into written will be >= 0 */
        if (dcb->ssl)
        {
            written = gw_write_SSL(dcb, local_writeq, &stop_writing);
        }
        else
        {
            written = gw_write(dcb, local_writeq, &stop_writing);
        }
        /*
         * If the stop_writing boolean is set, writing has become blocked,
         * so the remaining data is put back at the front of the write
         * queue.
         */
        if (stop_writing)
        {
            dcb->writeq = gwbuf_append(local_writeq, dcb->writeq);
            local_writeq = NULL;
        }
        else
        {
            /** Consume the bytes we have written from the list of buffers,
             * and increment the total bytes written. */
            local_writeq = gwbuf_consume(local_writeq, written);
            total_written += written;
        }
    }

    /* The write queue has drained, potentially need to call a callback function */
    dcb_call_callback(dcb, DCB_REASON_DRAINED);

    return total_written;
}

static void log_illegal_dcb(DCB *dcb)
{
    const char *connected_to;

    switch (dcb->dcb_role)
    {
    case DCB_ROLE_BACKEND_HANDLER:
        connected_to = dcb->server->unique_name;
        break;

    case DCB_ROLE_CLIENT_HANDLER:
        connected_to = dcb->remote;
        break;

    case DCB_ROLE_INTERNAL:
        connected_to = "Internal DCB";
        break;

    case DCB_ROLE_SERVICE_LISTENER:
        connected_to = dcb->service->name;
        break;

    default:
        connected_to = "Illegal DCB role";
        break;
    }

    MXS_ERROR("Removing DCB %p but it is in state %s which is not legal for "
              "a call to dcb_close. The DCB is connected to: %s", dcb,
              STRDCBSTATE(dcb->state), connected_to);
}

/**
 * Closes a client/backend dcb, which in the former case always means that
 * the corrsponding socket fd is closed and the dcb itself is freed, and in
 * latter case either the same as in the former or that the dcb is put into
 * the persistent pool.
 *
 * @param dcb The DCB to close
 */
void dcb_close(DCB *dcb)
{
    CHK_DCB(dcb);

#if defined(SS_DEBUG)
    int wid = Worker::get_current_id();
    if ((wid != -1) && (dcb->poll.thread.id != wid))
    {
        MXS_ALERT("dcb_close(%p) called by %d, owned by %d.",
                  dcb, wid, dcb->poll.thread.id);
        ss_dassert(dcb->poll.thread.id == Worker::get_current_id());
    }
#endif

    if ((DCB_STATE_UNDEFINED == dcb->state) || (DCB_STATE_DISCONNECTED == dcb->state))
    {
        log_illegal_dcb(dcb);
        raise(SIGABRT);
    }

    /**
     * dcb_close may be called for freshly created dcb, in which case
     * it only needs to be freed.
     */
    if (dcb->state == DCB_STATE_ALLOC && dcb->fd == DCBFD_CLOSED)
    {
        // A freshly created dcb that was closed before it was taken into use.
        dcb_final_free(dcb);
    }
    /*
     * If DCB is in persistent pool, mark it as an error and exit
     */
    else if (dcb->persistentstart > 0)
    {
        // A DCB in the persistent pool.

        // TODO: This dcb will now actually be closed when dcb_persistent_clean_count() is
        // TODO: called by either dcb_maybe_add_persistent() - another dcb is added to the
        // TODO: persistent pool - or server_get_persistent() - get a dcb from the persistent
        // TODO: pool - is called. There is no reason not to just remove this dcb from the
        // TODO: persistent pool here and now, and then close it immediately.
        dcb->dcb_errhandle_called = true;
    }
    else if (dcb->n_close == 0)
    {
        dcb->n_close = 1;

        if (dcb != dcb_get_current())
        {
            // If the dcb to be closed is *not* the dcb for which event callbacks are being
            // called, we close it immediately. Otherwise it will be closed once all callbacks
            // have been called, in the end of dcb_process_poll_events(), which currently is
            // above us in the call stack.
            dcb_final_close(dcb);
        }
    }
    else
    {
        ++dcb->n_close;
        // TODO: Will this happen on a regular basis?
        MXS_WARNING("dcb_close(%p) called %u times.", dcb, dcb->n_close);
    }
}

static void cb_dcb_close_in_owning_thread(int worker_id, void* data)
{
    DCB* dcb = static_cast<DCB*>(data);
    ss_dassert(dcb);

    dcb_close(dcb);
}

void dcb_close_in_owning_thread(DCB* dcb)
{
    // TODO: If someone now calls dcb_close(dcb) from the owning thread while
    // TODO: the dcb is being delivered to the owning thread, there will be a
    // TODO: crash when dcb_close(dcb) is called anew. Also dcbs should be
    // TODO: reference counted, so that we could addref before posting, thus
    // TODO: preventing too early a deletion.

    MXS_WORKER* worker = mxs_worker_get(dcb->poll.thread.id); // The owning worker
    ss_dassert(worker);

    intptr_t arg1 = (intptr_t)cb_dcb_close_in_owning_thread;
    intptr_t arg2 = (intptr_t)dcb;

    if (!mxs_worker_post_message(worker, MXS_WORKER_MSG_CALL, arg1, arg2))
    {
        MXS_ERROR("Could not post dcb for closing to the owning thread..");
    }
}

static void dcb_final_close(DCB* dcb)
{
#if defined(SS_DEBUG)
    int wid = Worker::get_current_id();
    if ((wid != -1) && (dcb->poll.thread.id != wid))
    {
        MXS_ALERT("dcb_final_close(%p) called by %d, owned by %d.",
                  dcb, wid, dcb->poll.thread.id);
        ss_dassert(dcb->poll.thread.id == Worker::get_current_id());
    }
#endif
    ss_dassert(dcb->n_close != 0);

    if (dcb->dcb_role == DCB_ROLE_BACKEND_HANDLER &&  // Backend DCB
        dcb->state == DCB_STATE_POLLING &&            // Being polled
        dcb->persistentstart == 0 &&                  // Not already in (> 0) or being evicted from (-1)
                                                      // the persistent pool.
        dcb->server)                                  // And has a server
    {
        /* May be a candidate for persistence, so save user name */
        const char *user;
        user = session_get_user(dcb->session);
        if (user && strlen(user) && !dcb->user)
        {
            dcb->user = MXS_STRDUP_A(user);
        }

        if (dcb_maybe_add_persistent(dcb))
        {
            dcb->n_close = 0;
        }
    }

    if (dcb->n_close != 0)
    {
        if (dcb->state == DCB_STATE_POLLING)
        {
            dcb_stop_polling_and_shutdown(dcb);
        }

        if (dcb->dcb_role == DCB_ROLE_CLIENT_HANDLER)
        {
            // TODO: If the role of the dcb is that of a client handler,
            // TODO: then dcb->service should be non-NULL, so there should
            // TODO: be no need for an if. Let's add an assert to see if
            // TODO: such a situation exists.
            ss_dassert(dcb->service);

            if (dcb->service)
            {
                atomic_add(&dcb->service->client_count, -1);
            }
        }

        if (dcb->server)
        {
            // This is now a DCB_ROLE_BACKEND_HANDLER.
            // TODO: Make decisions according to the role and assert
            // TODO: that what the role implies is preset.
            atomic_add(&dcb->server->stats.n_current, -1);
        }

        if (dcb->fd > 0)
        {
            // TODO: How could we get this far with a dcb->fd <= 0?

            if (close(dcb->fd) < 0)
            {
                int eno = errno;
                errno = 0;
                MXS_ERROR("Failed to close socket %d on dcb %p: %d, %s",
                          dcb->fd, dcb, eno, mxs_strerror(eno));
            }
            else
            {
                dcb->fd = DCBFD_CLOSED;

                MXS_DEBUG("Closed socket %d on dcb %p.", dcb->fd, dcb);
            }
        }

        dcb->state = DCB_STATE_DISCONNECTED;
        dcb_remove_from_list(dcb);
        dcb_final_free(dcb);
    }
}

/**
 * Add DCB to persistent pool if it qualifies, close otherwise
 *
 * @param dcb   The DCB to go to persistent pool or be closed
 * @return      bool - whether the DCB was added to the pool
 *
 */
static bool
dcb_maybe_add_persistent(DCB *dcb)
{
    if (dcb->user != NULL
        && (dcb->func.established == NULL || dcb->func.established(dcb))
        && strlen(dcb->user)
        && dcb->server
        && dcb->server->persistpoolmax
        && (dcb->server->status & SERVER_RUNNING)
        && !dcb->dcb_errhandle_called
        && !(dcb->flags & DCBF_HUNG)
        && dcb_persistent_clean_count(dcb, dcb->poll.thread.id, false) < dcb->server->persistpoolmax
        && dcb->server->stats.n_persistent < dcb->server->persistpoolmax)
    {
        DCB_CALLBACK *loopcallback;
        MXS_DEBUG("Adding DCB to persistent pool, user %s.", dcb->user);
        dcb->was_persistent = false;
        dcb->persistentstart = time(NULL);
        if (dcb->session)
            /*<
             * Terminate client session.
             */
        {
            MXS_SESSION *local_session = dcb->session;
            session_set_dummy(dcb);
            CHK_SESSION(local_session);
            if (SESSION_STATE_DUMMY != local_session->state)
            {
                session_put_ref(local_session);
            }
        }

        while ((loopcallback = dcb->callbacks) != NULL)
        {
            dcb->callbacks = loopcallback->next;
            MXS_FREE(loopcallback);
        }

        dcb->nextpersistent = dcb->server->persistent[dcb->poll.thread.id];
        dcb->server->persistent[dcb->poll.thread.id] = dcb;
        atomic_add(&dcb->server->stats.n_persistent, 1);
        atomic_add(&dcb->server->stats.n_current, -1);
        return true;
    }
    else if (dcb->dcb_role == DCB_ROLE_BACKEND_HANDLER && dcb->server)
    {
        MXS_DEBUG("Not adding DCB %p to persistent pool, "
                  "user %s, max for pool %ld, error handle called %s, hung flag %s, "
                  "server status %d, pool count %d.",
                  dcb, dcb->user ? dcb->user : "",
                  dcb->server->persistpoolmax,
                  dcb->dcb_errhandle_called ? "true" : "false",
                  (dcb->flags & DCBF_HUNG) ? "true" : "false",
                  dcb->server->status,
                  dcb->server->stats.n_persistent);
    }
    return false;
}

/**
 * Diagnostic to print a DCB
 *
 * @param dcb   The DCB to print
 *
 */
void
printDCB(DCB *dcb)
{
    printf("DCB: %p\n", (void *)dcb);
    printf("\tDCB state:            %s\n", gw_dcb_state2string(dcb->state));
    if (dcb->remote)
    {
        printf("\tConnected to:         %s\n", dcb->remote);
    }
    if (dcb->user)
    {
        printf("\tUsername:             %s\n", dcb->user);
    }
    if (dcb->protoname)
    {
        printf("\tProtocol:             %s\n", dcb->protoname);
    }
    if (dcb->writeq)
    {
        printf("\tQueued write data:    %u\n", gwbuf_length(dcb->writeq));
    }
    char *statusname = server_status(dcb->server);
    if (statusname)
    {
        printf("\tServer status:            %s\n", statusname);
        MXS_FREE(statusname);
    }
    char *rolename = dcb_role_name(dcb);
    if (rolename)
    {
        printf("\tRole:                     %s\n", rolename);
        MXS_FREE(rolename);
    }
    printf("\tStatistics:\n");
    printf("\t\tNo. of Reads:                       %d\n",
           dcb->stats.n_reads);
    printf("\t\tNo. of Writes:                      %d\n",
           dcb->stats.n_writes);
    printf("\t\tNo. of Buffered Writes:             %d\n",
           dcb->stats.n_buffered);
    printf("\t\tNo. of Accepts:                     %d\n",
           dcb->stats.n_accepts);
    printf("\t\tNo. of High Water Events:   %d\n",
           dcb->stats.n_high_water);
    printf("\t\tNo. of Low Water Events:    %d\n",
           dcb->stats.n_low_water);
}
/**
 * Display an entry from the spinlock statistics data
 *
 * @param       dcb     The DCB to print to
 * @param       desc    Description of the statistic
 * @param       value   The statistic value
 */
static void
spin_reporter(void *dcb, char *desc, int value)
{
    dcb_printf((DCB *)dcb, "\t\t%-40s  %d\n", desc, value);
}

bool printAllDCBs_cb(DCB *dcb, void *data)
{
    printDCB(dcb);
    return true;
}

/**
 * Diagnostic to print all DCB allocated in the system
 *
 */
void printAllDCBs()
{
    dcb_foreach(printAllDCBs_cb, NULL);
}

/**
 * Diagnostic to print one DCB in the system
 *
 * @param       pdcb    DCB to print results to
 * @param       dcb     DCB to be printed
 */
void
dprintOneDCB(DCB *pdcb, DCB *dcb)
{
    dcb_printf(pdcb, "DCB: %p\n", (void *)dcb);
    dcb_printf(pdcb, "\tDCB state:          %s\n",
               gw_dcb_state2string(dcb->state));
    if (dcb->session && dcb->session->service)
    {
        dcb_printf(pdcb, "\tService:            %s\n",
                   dcb->session->service->name);
    }
    if (dcb->remote)
    {
        dcb_printf(pdcb, "\tConnected to:       %s\n",
                   dcb->remote);
    }
    if (dcb->server)
    {
        if (dcb->server->name)
        {
            dcb_printf(pdcb, "\tServer name/IP:     %s\n",
                       dcb->server->name);
        }
        if (dcb->server->port)
        {
            dcb_printf(pdcb, "\tPort number:        %d\n",
                       dcb->server->port);
        }
    }
    if (dcb->user)
    {
        dcb_printf(pdcb, "\tUsername:           %s\n",
                   dcb->user);
    }
    if (dcb->protoname)
    {
        dcb_printf(pdcb, "\tProtocol:           %s\n",
                   dcb->protoname);
    }
    if (dcb->writeq)
    {
        dcb_printf(pdcb, "\tQueued write data:  %d\n",
                   gwbuf_length(dcb->writeq));
    }
    char *statusname = server_status(dcb->server);
    if (statusname)
    {
        dcb_printf(pdcb, "\tServer status:            %s\n", statusname);
        MXS_FREE(statusname);
    }
    char *rolename = dcb_role_name(dcb);
    if (rolename)
    {
        dcb_printf(pdcb, "\tRole:                     %s\n", rolename);
        MXS_FREE(rolename);
    }
    dcb_printf(pdcb, "\tStatistics:\n");
    dcb_printf(pdcb, "\t\tNo. of Reads:             %d\n", dcb->stats.n_reads);
    dcb_printf(pdcb, "\t\tNo. of Writes:            %d\n", dcb->stats.n_writes);
    dcb_printf(pdcb, "\t\tNo. of Buffered Writes:   %d\n", dcb->stats.n_buffered);
    dcb_printf(pdcb, "\t\tNo. of Accepts:           %d\n", dcb->stats.n_accepts);
    dcb_printf(pdcb, "\t\tNo. of High Water Events: %d\n", dcb->stats.n_high_water);
    dcb_printf(pdcb, "\t\tNo. of Low Water Events:  %d\n", dcb->stats.n_low_water);

    if (dcb->persistentstart)
    {
        char buff[20];
        struct tm timeinfo;
        localtime_r(&dcb->persistentstart, &timeinfo);
        strftime(buff, sizeof(buff), "%b %d %H:%M:%S", &timeinfo);
        dcb_printf(pdcb, "\t\tAdded to persistent pool:       %s\n", buff);
    }
}

static bool dprint_all_dcbs_cb(DCB *dcb, void *data)
{
    DCB *pdcb = (DCB*)data;
    dprintOneDCB(pdcb, dcb);
    return true;
}

/**
 * Diagnostic to print all DCB allocated in the system
 *
 * @param       pdcb    DCB to print results to
 */
void dprintAllDCBs(DCB *pdcb)
{
    dcb_foreach(dprint_all_dcbs_cb, pdcb);
}

static bool dlist_dcbs_cb(DCB *dcb, void *data)
{
    DCB *pdcb = (DCB*)data;
    dcb_printf(pdcb, " %-16p | %-26s | %-18s | %s\n",
               dcb, gw_dcb_state2string(dcb->state),
               ((dcb->session && dcb->session->service) ? dcb->session->service->name : ""),
               (dcb->remote ? dcb->remote : ""));
    return true;
}

/**
 * Diagnostic routine to print DCB data in a tabular form.
 *
 * @param       pdcb    DCB to print results to
 */
void
dListDCBs(DCB *pdcb)
{
    dcb_printf(pdcb, "Descriptor Control Blocks\n");
    dcb_printf(pdcb, "------------------+----------------------------+--------------------+----------\n");
    dcb_printf(pdcb, " %-16s | %-26s | %-18s | %s\n",
               "DCB", "State", "Service", "Remote");
    dcb_printf(pdcb, "------------------+----------------------------+--------------------+----------\n");
    dcb_foreach(dlist_dcbs_cb, pdcb);
    dcb_printf(pdcb, "------------------+----------------------------+--------------------+----------\n\n");
}

static bool dlist_clients_cb(DCB *dcb, void *data)
{
    DCB *pdcb = (DCB*)data;

    if (dcb->dcb_role == DCB_ROLE_CLIENT_HANDLER)
    {
        dcb_printf(pdcb, " %-15s | %16p | %-20s | %10p\n",
                   (dcb->remote ? dcb->remote : ""),
                   dcb, (dcb->session->service ?
                         dcb->session->service->name : ""),
                   dcb->session);
    }

    return true;
}

/**
 * Diagnostic routine to print client DCB data in a tabular form.
 *
 * @param       pdcb    DCB to print results to
 */
void
dListClients(DCB *pdcb)
{
    dcb_printf(pdcb, "Client Connections\n");
    dcb_printf(pdcb, "-----------------+------------------+----------------------+------------\n");
    dcb_printf(pdcb, " %-15s | %-16s | %-20s | %s\n",
               "Client", "DCB", "Service", "Session");
    dcb_printf(pdcb, "-----------------+------------------+----------------------+------------\n");
    dcb_foreach(dlist_clients_cb, pdcb);
    dcb_printf(pdcb, "-----------------+------------------+----------------------+------------\n\n");
}

/**
 * Diagnostic to print a DCB to another DCB
 *
 * @param pdcb  The DCB to which send the output
 * @param dcb   The DCB to print
 */
void
dprintDCB(DCB *pdcb, DCB *dcb)
{
    dcb_printf(pdcb, "DCB: %p\n", (void *)dcb);
    dcb_printf(pdcb, "\tDCB state:          %s\n", gw_dcb_state2string(dcb->state));
    if (dcb->session && dcb->session->service)
    {
        dcb_printf(pdcb, "\tService:            %s\n",
                   dcb->session->service->name);
    }
    if (dcb->remote)
    {
        dcb_printf(pdcb, "\tConnected to:               %s\n", dcb->remote);
    }
    if (dcb->user)
    {
        dcb_printf(pdcb, "\tUsername:                   %s\n",
                   dcb->user);
    }
    if (dcb->protoname)
    {
        dcb_printf(pdcb, "\tProtocol:                   %s\n",
                   dcb->protoname);
    }

    if (dcb->session && dcb->session->state != SESSION_STATE_DUMMY)
    {
        dcb_printf(pdcb, "\tOwning Session:     %" PRIu64 "\n", dcb->session->ses_id);
    }

    if (dcb->writeq)
    {
        dcb_printf(pdcb, "\tQueued write data:  %d\n", gwbuf_length(dcb->writeq));
    }
    if (dcb->delayq)
    {
        dcb_printf(pdcb, "\tDelayed write data: %d\n", gwbuf_length(dcb->delayq));
    }
    char *statusname = server_status(dcb->server);
    if (statusname)
    {
        dcb_printf(pdcb, "\tServer status:            %s\n", statusname);
        MXS_FREE(statusname);
    }
    char *rolename = dcb_role_name(dcb);
    if (rolename)
    {
        dcb_printf(pdcb, "\tRole:                     %s\n", rolename);
        MXS_FREE(rolename);
    }
    dcb_printf(pdcb, "\tStatistics:\n");
    dcb_printf(pdcb, "\t\tNo. of Reads:                     %d\n",
               dcb->stats.n_reads);
    dcb_printf(pdcb, "\t\tNo. of Writes:                    %d\n",
               dcb->stats.n_writes);
    dcb_printf(pdcb, "\t\tNo. of Buffered Writes:           %d\n",
               dcb->stats.n_buffered);
    dcb_printf(pdcb, "\t\tNo. of Accepts:                   %d\n",
               dcb->stats.n_accepts);
    dcb_printf(pdcb, "\t\tNo. of High Water Events: %d\n",
               dcb->stats.n_high_water);
    dcb_printf(pdcb, "\t\tNo. of Low Water Events:  %d\n",
               dcb->stats.n_low_water);

    if (dcb->persistentstart)
    {
        char buff[20];
        struct tm timeinfo;
        localtime_r(&dcb->persistentstart, &timeinfo);
        strftime(buff, sizeof(buff), "%b %d %H:%M:%S", &timeinfo);
        dcb_printf(pdcb, "\t\tAdded to persistent pool:       %s\n", buff);
    }
}

/**
 * Return a string representation of a DCB state.
 *
 * @param state The DCB state
 * @return String representation of the state
 *
 */
const char *
gw_dcb_state2string(dcb_state_t state)
{
    switch (state)
    {
    case DCB_STATE_ALLOC:
        return "DCB Allocated";
    case DCB_STATE_POLLING:
        return "DCB in the polling loop";
    case DCB_STATE_NOPOLLING:
        return "DCB not in polling loop";
    case DCB_STATE_LISTENING:
        return "DCB for listening socket";
    case DCB_STATE_DISCONNECTED:
        return "DCB socket closed";
    case DCB_STATE_UNDEFINED:
        return "DCB undefined state";
    default:
        return "DCB (unknown - erroneous)";
    }
}

/**
 * A  DCB based wrapper for printf. Allows formatting printing to
 * a descriptor control block.
 *
 * @param dcb   Descriptor to write to
 * @param fmt   A printf format string
 * @param ...   Variable arguments for the print format
 */
void
dcb_printf(DCB *dcb, const char *fmt, ...)
{
    GWBUF   *buf;
    va_list args;

    if ((buf = gwbuf_alloc(10240)) == NULL)
    {
        return;
    }
    va_start(args, fmt);
    vsnprintf((char*)GWBUF_DATA(buf), 10240, fmt, args);
    va_end(args);

    buf->end = (void *)((char *)GWBUF_DATA(buf) + strlen((char*)GWBUF_DATA(buf)));
    dcb->func.write(dcb, buf);
}

/**
 * Print hash table statistics to a DCB
 *
 * @param dcb           The DCB to send the information to
 * @param table         The hash table
 */
void dcb_hashtable_stats(
    DCB     *dcb,
    void    *table)
{
    int total;
    int longest;
    int hashsize;

    total = 0;
    longest = 0;

    hashtable_get_stats(table, &hashsize, &total, &longest);

    dcb_printf(dcb,
               "Hashtable: %p, size %d\n",
               table,
               hashsize);

    dcb_printf(dcb, "\tNo. of entries:      %d\n", total);
    dcb_printf(dcb,
               "\tAverage chain length:        %.1f\n",
               (hashsize == 0 ? (float)hashsize : (float)total / hashsize));
    dcb_printf(dcb, "\tLongest chain length:        %d\n", longest);
}

/**
 * Write data to a DCB socket through an SSL structure. The SSL structure is
 * linked from the DCB. All communication is encrypted and done via the SSL
 * structure. Data is written from the DCB write queue.
 *
 * @param dcb           The DCB having an SSL connection
 * @param writeq        A buffer list containing the data to be written
 * @param stop_writing  Set to true if the caller should stop writing, false otherwise
 * @return              Number of written bytes
 */
static int
gw_write_SSL(DCB *dcb, GWBUF *writeq, bool *stop_writing)
{
    int written;

    written = SSL_write(dcb->ssl, GWBUF_DATA(writeq), GWBUF_LENGTH(writeq));

    *stop_writing = false;
    switch ((SSL_get_error(dcb->ssl, written)))
    {
    case SSL_ERROR_NONE:
        /* Successful write */
        dcb->ssl_write_want_read = false;
        dcb->ssl_write_want_write = false;
        break;

    case SSL_ERROR_ZERO_RETURN:
        /* react to the SSL connection being closed */
        *stop_writing = true;
        poll_fake_hangup_event(dcb);
        break;

    case SSL_ERROR_WANT_READ:
        /* Prevent SSL I/O on connection until retried, return to poll loop */
        *stop_writing = true;
        dcb->ssl_write_want_read = true;
        dcb->ssl_write_want_write = false;
        break;

    case SSL_ERROR_WANT_WRITE:
        /* Prevent SSL I/O on connection until retried, return to poll loop */
        *stop_writing = true;
        dcb->ssl_write_want_read = false;
        dcb->ssl_write_want_write = true;
        break;

    case SSL_ERROR_SYSCALL:
        *stop_writing = true;
        if (dcb_log_errors_SSL(dcb, written) < 0)
        {
            poll_fake_hangup_event(dcb);
        }
        break;

    default:
        /* Report error(s) and shutdown the connection */
        *stop_writing = true;
        if (dcb_log_errors_SSL(dcb, written) < 0)
        {
            poll_fake_hangup_event(dcb);
        }
        break;
    }

    return written > 0 ? written : 0;
}

/**
 * Write data to a DCB. The data is taken from the DCB's write queue.
 *
 * @param dcb           The DCB to write buffer
 * @param writeq        A buffer list containing the data to be written
 * @param stop_writing  Set to true if the caller should stop writing, false otherwise
 * @return              Number of written bytes
 */
static int
gw_write(DCB *dcb, GWBUF *writeq, bool *stop_writing)
{
    int written = 0;
    int fd = dcb->fd;
    size_t nbytes = GWBUF_LENGTH(writeq);
    void *buf = GWBUF_DATA(writeq);
    int saved_errno;

    errno = 0;

    if (fd > 0)
    {
        written = write(fd, buf, nbytes);
    }

    saved_errno = errno;
    errno = 0;

    if (written < 0)
    {
        *stop_writing = true;
#if defined(SS_DEBUG)
        if (saved_errno != EAGAIN &&
            saved_errno != EWOULDBLOCK)
#else
        if (saved_errno != EAGAIN &&
            saved_errno != EWOULDBLOCK &&
            saved_errno != EPIPE)
#endif
        {
            MXS_ERROR("Write to %s %s in state %s failed: %d, %s",
                      DCB_STRTYPE(dcb), dcb->remote, STRDCBSTATE(dcb->state),
                      saved_errno, mxs_strerror(saved_errno));
        }
    }
    else
    {
        *stop_writing = false;
    }

    return written > 0 ? written : 0;
}

/**
 * Add a callback
 *
 * Duplicate registrations are not allowed, therefore an error will be
 * returned if the specific function, reason and userdata triple
 * are already registered.
 * An error will also be returned if the is insufficient memeory available to
 * create the registration.
 *
 * @param dcb           The DCB to add the callback to
 * @param reason        The callback reason
 * @param callback      The callback function to call
 * @param userdata      User data to send in the call
 * @return              Non-zero (true) if the callback was added
 */
int
dcb_add_callback(DCB *dcb,
                 DCB_REASON reason,
                 int (*callback)(struct dcb *, DCB_REASON, void *),
                 void *userdata)
{
    DCB_CALLBACK *cb, *ptr, *lastcb = NULL;

    if ((ptr = (DCB_CALLBACK *)MXS_MALLOC(sizeof(DCB_CALLBACK))) == NULL)
    {
        return 0;
    }
    ptr->reason = reason;
    ptr->cb = callback;
    ptr->userdata = userdata;
    ptr->next = NULL;
    cb = dcb->callbacks;

    while (cb)
    {
        if (cb->reason == reason && cb->cb == callback &&
            cb->userdata == userdata)
        {
            /* Callback is a duplicate, abandon it */
            MXS_FREE(ptr);
            return 0;
        }
        lastcb = cb;
        cb = cb->next;
    }
    if (NULL == lastcb)
    {
        dcb->callbacks = ptr;
    }
    else
    {
        lastcb->next = ptr;
    }

    return 1;
}

/**
 * Remove a callback from the callback list for the DCB
 *
 * Searches down the linked list to find the callback with a matching reason, function
 * and userdata.
 *
 * @param dcb           The DCB to add the callback to
 * @param reason        The callback reason
 * @param callback      The callback function to call
 * @param userdata      User data to send in the call
 * @return              Non-zero (true) if the callback was removed
 */
int
dcb_remove_callback(DCB *dcb,
                    DCB_REASON reason,
                    int (*callback)(struct dcb *, DCB_REASON, void *),
                    void *userdata)
{
    DCB_CALLBACK *cb, *pcb = NULL;
    int          rval = 0;
    cb = dcb->callbacks;

    if (cb == NULL)
    {
        rval = 0;
    }
    else
    {
        while (cb)
        {
            if (cb->reason == reason &&
                cb->cb == callback &&
                cb->userdata == userdata)
            {
                if (pcb != NULL)
                {
                    pcb->next = cb->next;
                }
                else
                {
                    dcb->callbacks = cb->next;
                }

                MXS_FREE(cb);
                rval = 1;
                break;
            }
            pcb = cb;
            cb = cb->next;
        }
    }

    return rval;
}

/**
 * Call the set of callbacks registered for a particular reason.
 *
 * @param dcb           The DCB to call the callbacks regarding
 * @param reason        The reason that has triggered the call
 */
static void
dcb_call_callback(DCB *dcb, DCB_REASON reason)
{
    DCB_CALLBACK *cb, *nextcb;
    cb = dcb->callbacks;

    while (cb)
    {
        if (cb->reason == reason)
        {
            nextcb = cb->next;
            cb->cb(dcb, reason, cb->userdata);
            cb = nextcb;
        }
        else
        {
            cb = cb->next;
        }
    }
}

/**
 * Check the passed DCB to ensure it is in the list of all DCBS
 *
 * @param       dcb     The DCB to check
 * @return      1 if the DCB is in the list, otherwise 0
 */
int
dcb_isvalid(DCB *dcb)
{
    return !!dcb;
}

static void dcb_hangup_foreach_worker(int thread_id, struct server* server)
{
    for (DCB *dcb = this_unit.all_dcbs[thread_id]; dcb; dcb = dcb->thread.next)
    {
        if (dcb->state == DCB_STATE_POLLING && dcb->server &&
            dcb->server == server)
        {
            poll_fake_hangup_event(dcb);
        }
    }
}

/**
 * Call all the callbacks on all DCB's that match the server and the reason given
 *
 * @param reason        The DCB_REASON that triggers the callback
 */
void
dcb_hangup_foreach(struct server* server)
{
    intptr_t arg1 = (intptr_t)dcb_hangup_foreach_worker;
    intptr_t arg2 = (intptr_t)server;

    Worker::broadcast_message(MXS_WORKER_MSG_CALL, arg1, arg2);
}

/**
 * Check persistent pool for expiry or excess size and count
 *
 * @param dcb           The DCB being closed.
 * @param id            Thread ID
 * @param cleanall      Boolean, if true the whole pool is cleared for the
 *                      server related to the given DCB
 * @return              A count of the DCBs remaining in the pool
 */
int
dcb_persistent_clean_count(DCB *dcb, int id, bool cleanall)
{
    int count = 0;
    if (dcb && dcb->server)
    {
        SERVER *server = dcb->server;
        DCB *previousdcb = NULL;
        DCB *persistentdcb, *nextdcb;
        DCB *disposals = NULL;

        CHK_SERVER(server);
        persistentdcb = server->persistent[id];
        while (persistentdcb)
        {
            CHK_DCB(persistentdcb);
            nextdcb = persistentdcb->nextpersistent;
            if (cleanall
                || persistentdcb-> dcb_errhandle_called
                || count >= server->persistpoolmax
                || persistentdcb->server == NULL
                || !(persistentdcb->server->status & SERVER_RUNNING)
                || (time(NULL) - persistentdcb->persistentstart) > server->persistmaxtime)
            {
                /* Remove from persistent pool */
                if (previousdcb)
                {
                    previousdcb->nextpersistent = nextdcb;
                }
                else
                {
                    server->persistent[id] = nextdcb;
                }
                /* Add removed DCBs to disposal list for processing outside spinlock */
                persistentdcb->nextpersistent = disposals;
                disposals = persistentdcb;
                atomic_add(&server->stats.n_persistent, -1);
            }
            else
            {
                count++;
                previousdcb = persistentdcb;
            }
            persistentdcb = nextdcb;
        }
        server->persistmax = MXS_MAX(server->persistmax, count);

        /** Call possible callback for this DCB in case of close */
        while (disposals)
        {
            nextdcb = disposals->nextpersistent;
            disposals->persistentstart = -1;
            if (DCB_STATE_POLLING == disposals->state)
            {
                dcb_stop_polling_and_shutdown(disposals);
            }
            dcb_close(disposals);
            disposals = nextdcb;
        }
    }
    return count;
}

struct dcb_usage_count
{
    int count;
    DCB_USAGE type;
};

bool count_by_usage_cb(DCB *dcb, void *data)
{
    struct dcb_usage_count *d = (struct dcb_usage_count*)data;

    switch (d->type)
    {
    case DCB_USAGE_CLIENT:
        if (DCB_ROLE_CLIENT_HANDLER == dcb->dcb_role)
        {
            d->count++;
        }
        break;
    case DCB_USAGE_LISTENER:
        if (dcb->state == DCB_STATE_LISTENING)
        {
            d->count++;
        }
        break;
    case DCB_USAGE_BACKEND:
        if (dcb->dcb_role == DCB_ROLE_BACKEND_HANDLER)
        {
            d->count++;
        }
        break;
    case DCB_USAGE_INTERNAL:
        if (dcb->dcb_role == DCB_ROLE_CLIENT_HANDLER ||
            dcb->dcb_role == DCB_ROLE_BACKEND_HANDLER)
        {
            d->count++;
        }
        break;
    case DCB_USAGE_ALL:
        d->count++;
        break;
    }

    return true;
}

/**
 * Return DCB counts optionally filtered by usage
 *
 * @param       usage   The usage of the DCB
 * @return      A count of DCBs in the desired state
 */
int
dcb_count_by_usage(DCB_USAGE usage)
{
    struct dcb_usage_count val = {};
    val.count = 0;
    val.type = usage;

    dcb_foreach(count_by_usage_cb, &val);

    return val.count;
}

/**
 * Create the SSL structure for this DCB.
 * This function creates the SSL structure for the given SSL context.
 * This context should be the context of the service.
 * @param       dcb
 * @return      -1 on error, 0 otherwise.
 */
static int
dcb_create_SSL(DCB* dcb, SSL_LISTENER *ssl)
{
    if ((dcb->ssl = SSL_new(ssl->ctx)) == NULL)
    {
        MXS_ERROR("Failed to initialize SSL for connection.");
        return -1;
    }

    if (SSL_set_fd(dcb->ssl, dcb->fd) == 0)
    {
        MXS_ERROR("Failed to set file descriptor for SSL connection.");
        return -1;
    }

    return 0;
}

/**
 * Accept a SSL connection and do the SSL authentication handshake.
 * This function accepts a client connection to a DCB. It assumes that the SSL
 * structure has the underlying method of communication set and this method is ready
 * for usage. It then proceeds with the SSL handshake and stops only if an error
 * occurs or the client has not yet written enough data to complete the handshake.
 * @param dcb DCB which should accept the SSL connection
 * @return 1 if the handshake was successfully completed, 0 if the handshake is
 * still ongoing and another call to dcb_SSL_accept should be made or -1 if an
 * error occurred during the handshake and the connection should be terminated.
 */
int dcb_accept_SSL(DCB* dcb)
{
    if ((NULL == dcb->listener || NULL == dcb->listener->ssl) ||
        (NULL == dcb->ssl && dcb_create_SSL(dcb, dcb->listener->ssl) != 0))
    {
        return -1;
    }

    ss_debug(const char *remote = dcb->remote ? dcb->remote : "");
    ss_debug(const char *user = dcb->user ? dcb->user : "");

    int ssl_rval = SSL_accept(dcb->ssl);

    switch (SSL_get_error(dcb->ssl, ssl_rval))
    {
    case SSL_ERROR_NONE:
        MXS_DEBUG("SSL_accept done for %s@%s", user, remote);
        dcb->ssl_state = SSL_ESTABLISHED;
        dcb->ssl_read_want_write = false;
        return 1;

    case SSL_ERROR_WANT_READ:
        MXS_DEBUG("SSL_accept ongoing want read for %s@%s", user, remote);
        return 0;

    case SSL_ERROR_WANT_WRITE:
        MXS_DEBUG("SSL_accept ongoing want write for %s@%s", user, remote);
        dcb->ssl_read_want_write = true;
        return 0;

    case SSL_ERROR_ZERO_RETURN:
        MXS_DEBUG("SSL error, shut down cleanly during SSL accept %s@%s", user, remote);
        dcb_log_errors_SSL(dcb, 0);
        poll_fake_hangup_event(dcb);
        return 0;

    case SSL_ERROR_SYSCALL:
        MXS_DEBUG("SSL connection SSL_ERROR_SYSCALL error during accept %s@%s", user, remote);
        if (dcb_log_errors_SSL(dcb, ssl_rval) < 0)
        {
            dcb->ssl_state = SSL_HANDSHAKE_FAILED;
            poll_fake_hangup_event(dcb);
            return -1;
        }
        else
        {
            return 0;
        }

    default:
        MXS_DEBUG("SSL connection shut down with error during SSL accept %s@%s", user, remote);
        if (dcb_log_errors_SSL(dcb, ssl_rval) < 0)
        {
            dcb->ssl_state = SSL_HANDSHAKE_FAILED;
            poll_fake_hangup_event(dcb);
            return -1;
        }
        else
        {
            return 0;
        }
    }
}

/**
 * Initiate an SSL client connection to a server
 *
 * This functions starts an SSL client connection to a server which is expecting
 * an SSL handshake. The DCB should already have a TCP connection to the server and
 * this connection should be in a state that expects an SSL handshake.
 * THIS CODE IS UNUSED AND UNTESTED as at 4 Jan 2016
 * @param dcb DCB to connect
 * @return 1 on success, -1 on error and 0 if the SSL handshake is still ongoing
 */
int dcb_connect_SSL(DCB* dcb)
{
    int ssl_rval;
    int return_code;

    if ((NULL == dcb->server || NULL == dcb->server->server_ssl) ||
        (NULL == dcb->ssl && dcb_create_SSL(dcb, dcb->server->server_ssl) != 0))
    {
        ss_dassert((NULL != dcb->server) && (NULL != dcb->server->server_ssl));
        return -1;
    }
    dcb->ssl_state = SSL_HANDSHAKE_REQUIRED;
    ssl_rval = SSL_connect(dcb->ssl);
    switch (SSL_get_error(dcb->ssl, ssl_rval))
    {
    case SSL_ERROR_NONE:
        MXS_DEBUG("SSL_connect done for %s", dcb->remote);
        dcb->ssl_state = SSL_ESTABLISHED;
        dcb->ssl_read_want_write = false;
        return_code = 1;
        break;

    case SSL_ERROR_WANT_READ:
        MXS_DEBUG("SSL_connect ongoing want read for %s", dcb->remote);
        return_code = 0;
        break;

    case SSL_ERROR_WANT_WRITE:
        MXS_DEBUG("SSL_connect ongoing want write for %s", dcb->remote);
        dcb->ssl_read_want_write = true;
        return_code = 0;
        break;

    case SSL_ERROR_ZERO_RETURN:
        MXS_DEBUG("SSL error, shut down cleanly during SSL connect %s", dcb->remote);
        if (dcb_log_errors_SSL(dcb, 0) < 0)
        {
            poll_fake_hangup_event(dcb);
        }
        return_code = 0;
        break;

    case SSL_ERROR_SYSCALL:
        MXS_DEBUG("SSL connection shut down with SSL_ERROR_SYSCALL during SSL connect %s", dcb->remote);
        if (dcb_log_errors_SSL(dcb, ssl_rval) < 0)
        {
            dcb->ssl_state = SSL_HANDSHAKE_FAILED;
            poll_fake_hangup_event(dcb);
            return_code = -1;
        }
        else
        {
            return_code = 0;
        }
        break;

    default:
        MXS_DEBUG("SSL connection shut down with error during SSL connect %s", dcb->remote);
        if (dcb_log_errors_SSL(dcb, ssl_rval) < 0)
        {
            dcb->ssl_state = SSL_HANDSHAKE_FAILED;
            poll_fake_hangup_event(dcb);
            return -1;
        }
        else
        {
            return 0;
        }
        break;
    }
    return return_code;
}

/**
 * @brief Accept a new client connection, given a listener, return new DCB
 *
 * Calls dcb_accept_one_connection to do the basic work of obtaining a new
 * connection from a listener.  If that succeeds, some settings are fixed and
 * a client DCB is created to handle the new connection. Further DCB details
 * are set before returning the new DCB to the caller, or returning NULL if
 * no new connection could be achieved.
 *
 * @param dcb Listener DCB that has detected new connection request
 * @return DCB - The new client DCB for the new connection, or NULL if failed
 */
DCB *
dcb_accept(DCB *listener)
{
    DCB *client_dcb = NULL;
    MXS_PROTOCOL *protocol_funcs = &listener->func;
    int c_sock;
    int sendbuf;
    struct sockaddr_storage client_conn;
    socklen_t optlen = sizeof(sendbuf);

    if ((c_sock = dcb_accept_one_connection(listener, (struct sockaddr *)&client_conn)) >= 0)
    {
        listener->stats.n_accepts++;

        /* set nonblocking  */
        sendbuf = MXS_CLIENT_SO_SNDBUF;

        if (setsockopt(c_sock, SOL_SOCKET, SO_SNDBUF, &sendbuf, optlen) != 0)
        {
            MXS_ERROR("Failed to set socket options: %d, %s", errno, mxs_strerror(errno));
        }

        sendbuf = MXS_CLIENT_SO_RCVBUF;

        if (setsockopt(c_sock, SOL_SOCKET, SO_RCVBUF, &sendbuf, optlen) != 0)
        {
            MXS_ERROR("Failed to set socket options: %d, %s", errno, mxs_strerror(errno));
        }
        setnonblocking(c_sock);

        client_dcb = dcb_alloc(DCB_ROLE_CLIENT_HANDLER, listener->listener);

        if (client_dcb == NULL)
        {
            MXS_ERROR("Failed to create DCB object for client connection.");
            close(c_sock);
        }
        else
        {
            const char *authenticator_name = "NullAuthDeny";
            MXS_AUTHENTICATOR *authfuncs;

            client_dcb->service = listener->session->service;
            client_dcb->session = session_set_dummy(client_dcb);
            client_dcb->fd = c_sock;

            // get client address
            if (client_conn.ss_family == AF_UNIX)
            {
                // client address
                client_dcb->remote = MXS_STRDUP_A("localhost");
            }
            else
            {
                /* client IP in raw data*/
                memcpy(&client_dcb->ip, &client_conn, sizeof(client_conn));
                /* client IP in string representation */
                client_dcb->remote = (char *)MXS_CALLOC(INET6_ADDRSTRLEN + 1, sizeof(char));

                if (client_dcb->remote)
                {
                    void *ptr;
                    if (client_dcb->ip.ss_family == AF_INET)
                    {
                        ptr = &((struct sockaddr_in*)&client_dcb->ip)->sin_addr;
                    }
                    else
                    {
                        ptr = &((struct sockaddr_in6*)&client_dcb->ip)->sin6_addr;
                    }

                    inet_ntop(client_dcb->ip.ss_family, ptr,
                              client_dcb->remote, INET6_ADDRSTRLEN);
                }
            }
            memcpy(&client_dcb->func, protocol_funcs, sizeof(MXS_PROTOCOL));
            if (listener->listener->authenticator)
            {
                authenticator_name = listener->listener->authenticator;
            }
            else if (client_dcb->func.auth_default != NULL)
            {
                authenticator_name = client_dcb->func.auth_default();
            }
            if ((authfuncs = (MXS_AUTHENTICATOR *)load_module(authenticator_name,
                                                              MODULE_AUTHENTICATOR)) == NULL)
            {
                if ((authfuncs = (MXS_AUTHENTICATOR *)load_module("NullAuthDeny",
                                                                  MODULE_AUTHENTICATOR)) == NULL)
                {
                    MXS_ERROR("Failed to load authenticator module '%s'", authenticator_name);
                    dcb_close(client_dcb);
                    return NULL;
                }
            }
            memcpy(&(client_dcb->authfunc), authfuncs, sizeof(MXS_AUTHENTICATOR));

            /** Allocate DCB specific authentication data */
            if (client_dcb->authfunc.create &&
                (client_dcb->authenticator_data = client_dcb->authfunc.create(
                                                      client_dcb->listener->auth_instance)) == NULL)
            {
                MXS_ERROR("Failed to create authenticator for client DCB");
                dcb_close(client_dcb);
                return NULL;
            }

            if (client_dcb->service->max_connections &&
                client_dcb->service->client_count >= client_dcb->service->max_connections)
            {
                if (!mxs_enqueue(client_dcb->service->queued_connections, client_dcb))
                {
                    if (client_dcb->func.connlimit)
                    {
                        client_dcb->func.connlimit(client_dcb, client_dcb->service->max_connections);
                    }
                    dcb_close(client_dcb);
                }
                client_dcb = NULL;
            }
        }
    }
    return client_dcb;
}

/**
 * @brief Accept a new client connection, given listener, return file descriptor
 *
 * Up to 10 retries will be attempted in case of non-permanent errors.  Calls
 * the accept function and analyses the return, logging any errors and making
 * an appropriate return.
 *
 * @param dcb Listener DCB that has detected new connection request
 * @return -1 for failure, or a file descriptor for the new connection
 */
static int
dcb_accept_one_connection(DCB *listener, struct sockaddr *client_conn)
{
    int c_sock;

    /* Try up to 10 times to get a file descriptor by use of accept */
    for (int i = 0; i < 10; i++)
    {
        socklen_t client_len = sizeof(struct sockaddr_storage);
        int eno = 0;

        /* new connection from client */
        c_sock = accept(listener->fd,
                        client_conn,
                        &client_len);
        eno = errno;
        errno = 0;

        if (c_sock == -1)
        {
            /* Did not get a file descriptor */
            if (eno == EAGAIN || eno == EWOULDBLOCK)
            {
                /**
                 * We have processed all incoming connections, break out
                 * of loop for return of -1.
                 */
                break;
            }
            else if (eno == ENFILE || eno == EMFILE)
            {
                struct timespec ts1;
                long long nanosecs;

                /**
                 * Exceeded system's (ENFILE) or processes
                 * (EMFILE) max. number of files limit.
                 */

                /* Log an error the first time this happens */
                if (i == 0)
                {
                    MXS_ERROR("Failed to accept new client connection: %d, %s",
                              eno, mxs_strerror(eno));
                }
                nanosecs = (long long)1000000 * 100 * i * i;
                ts1.tv_sec = nanosecs / 1000000000;
                ts1.tv_nsec = nanosecs % 1000000000;
                nanosleep(&ts1, NULL);

                /* Remain in loop for up to the loop limit, retries. */
            }
            else
            {
                /**
                 * Other error, log it then break out of loop for return of -1.
                 */
                MXS_ERROR("Failed to accept new client connection: %d, %s",
                          eno, mxs_strerror(eno));
                break;
            }
        }
        else
        {
            break;
        }
    }
    return c_sock;
}

/**
 * @brief Create a listener, add new information to the given DCB
 *
 * First creates and opens a socket, either TCP or Unix according to the
 * configuration data provided.  Then try to listen on the socket and
 * record the socket in the given DCB.  Add the given DCB into the poll
 * list.  The protocol name does not affect the logic, but is used in
 * log messages.
 *
 * @param listener Listener DCB that is being created
 * @param config Configuration for port to listen on
 * @param protocol_name Name of protocol that is listening
 * @return 0 if new listener created successfully, otherwise -1
 */
int dcb_listen(DCB *listener, const char *config, const char *protocol_name)
{
    char host[strlen(config) + 1];
    strcpy(host, config);
    char *port_str = strrchr(host, '|');
    uint16_t port = 0;

    if (port_str)
    {
        *port_str++ = 0;
        port = atoi(port_str);
    }

    int listener_socket = -1;

    if (strchr(host, '/'))
    {
        listener_socket = dcb_listen_create_socket_unix(host);
    }
    else if (port > 0)
    {
        listener_socket = dcb_listen_create_socket_inet(host, port);

        if (listener_socket == -1 && strcmp(host, "::") == 0)
        {
            /** Attempt to bind to the IPv4 if the default IPv6 one is used */
            MXS_WARNING("Failed to bind on default IPv6 host '::', attempting "
                        "to bind on IPv4 version '0.0.0.0'");
            strcpy(host, "0.0.0.0");
            listener_socket = dcb_listen_create_socket_inet(host, port);
        }
    }
    else
    {
        // We don't have a socket path or a network port
        ss_dassert(false);
    }

    if (listener_socket < 0)
    {
        ss_dassert(listener_socket == -1);
        return -1;
    }

    /**
     * The use of INT_MAX for backlog length in listen() allows the end-user to
     * control the backlog length with the net.ipv4.tcp_max_syn_backlog kernel
     * option since the parameter is silently truncated to the configured value.
     *
     * @see man 2 listen
     */
    if (listen(listener_socket, INT_MAX) != 0)
    {
        MXS_ERROR("Failed to start listening on [%s]:%u with protocol '%s': %d, %s",
                  host, port, protocol_name, errno, mxs_strerror(errno));
        close(listener_socket);
        return -1;
    }

    MXS_NOTICE("Listening for connections at [%s]:%u with protocol %s", host, port, protocol_name);

    // assign listener_socket to dcb
    listener->fd = listener_socket;

    // add listening socket to poll structure
    if (poll_add_dcb(listener) != 0)
    {
        MXS_ERROR("MaxScale encountered system limit while "
                  "attempting to register on an epoll instance.");
        return -1;
    }
    return 0;
}

/**
 * @brief Create a network listener socket
 *
 * @param host The network address to listen on
 * @param port The port to listen on
 * @return     The opened socket or -1 on error
 */
static int dcb_listen_create_socket_inet(const char *host, uint16_t port)
{
    struct sockaddr_storage server_address = {};
    return open_network_socket(MXS_SOCKET_LISTENER, &server_address, host, port);
}

/**
 * @brief Create a Unix domain socket
 *
 * @param path The socket path
 * @return     The opened socket or -1 on error
 */
static int dcb_listen_create_socket_unix(const char *path)
{
    if (unlink(path) == -1 && errno != ENOENT)
    {
        MXS_ERROR("Failed to unlink Unix Socket %s: %d %s",
                  path, errno, mxs_strerror(errno));
    }

    struct sockaddr_un local_addr;
    int listener_socket = open_unix_socket(MXS_SOCKET_LISTENER, &local_addr, path);

    if (listener_socket >= 0 && chmod(path, 0777) < 0)
    {
        MXS_ERROR("Failed to change permissions on UNIX Domain socket '%s': %d, %s",
                  path, errno, mxs_strerror(errno));
    }

    return listener_socket;
}

/**
 * @brief Set socket options, log an error if fails
 *
 * Simply calls the setsockopt function with the same parameters, but also
 * checks for success and logs an error if necessary.
 *
 * @param sockfd  Socket file descriptor
 * @param level   Will always be SOL_SOCKET for socket level operations
 * @param optname Option name
 * @param optval  Option value
 * @param optlen  Length of option value
 * @return 0 if successful, otherwise -1
 */
static int
dcb_set_socket_option(int sockfd, int level, int optname, void *optval, socklen_t optlen)
{
    if (setsockopt(sockfd, level, optname, optval, optlen) != 0)
    {
        MXS_ERROR("Failed to set socket options: %d, %s",
                  errno, mxs_strerror(errno));
        return -1;
    }
    return 0;
}

/**
 * Convert a DCB role to a string, the returned
 * string has been malloc'd and must be free'd by the caller
 *
 * @param DCB    The DCB to return the role of
 * @return       A string representation of the DCB role
 */
char *
dcb_role_name(DCB *dcb)
{
    char *name = (char *)MXS_MALLOC(64);

    if (name)
    {
        name[0] = 0;
        if (DCB_ROLE_SERVICE_LISTENER == dcb->dcb_role)
        {
            strcat(name, "Service Listener");
        }
        else if (DCB_ROLE_CLIENT_HANDLER == dcb->dcb_role)
        {
            strcat(name, "Client Request Handler");
        }
        else if (DCB_ROLE_BACKEND_HANDLER == dcb->dcb_role)
        {
            strcat(name, "Backend Request Handler");
        }
        else if (DCB_ROLE_INTERNAL == dcb->dcb_role)
        {
            strcat(name, "Internal");
        }
        else
        {
            strcat(name, "Unknown");
        }
    }
    return name;
}

/**
 * @brief Append a buffer the DCB's readqueue
 *
 * Usually data is stored into the DCB's readqueue when not enough data is
 * available and the processing needs to be deferred until more data is available.
 *
 * @param dcb DCB where the buffer is stored
 * @param buffer Buffer to store
 */
void dcb_append_readqueue(DCB *dcb, GWBUF *buffer)
{
    dcb->dcb_readqueue = gwbuf_append(dcb->dcb_readqueue, buffer);
}


static void dcb_add_to_worker_list(int thread_id, void* data)
{
    DCB *dcb = (DCB*)data;

    ss_dassert(thread_id == dcb->poll.thread.id);

    dcb_add_to_list(dcb);
}

void dcb_add_to_list(DCB *dcb)
{
    if (dcb->dcb_role != DCB_ROLE_SERVICE_LISTENER ||
        (dcb->thread.next == NULL && dcb->thread.tail == NULL))
    {
        /**
         * This is a DCB which is either not a listener or it is a listener which
         * is not in the list. Stopped listeners are not removed from the list.
         */

        int worker_id = Worker::get_current_id();

        if (worker_id == dcb->poll.thread.id)
        {
            if (this_unit.all_dcbs[dcb->poll.thread.id] == NULL)
            {
                this_unit.all_dcbs[dcb->poll.thread.id] = dcb;
                this_unit.all_dcbs[dcb->poll.thread.id]->thread.tail = dcb;
            }
            else
            {
                this_unit.all_dcbs[dcb->poll.thread.id]->thread.tail->thread.next = dcb;
                this_unit.all_dcbs[dcb->poll.thread.id]->thread.tail = dcb;
            }
        }
        else
        {
            Worker* worker = Worker::get(dcb->poll.thread.id);
            ss_dassert(worker);

            intptr_t arg1 = (intptr_t)dcb_add_to_worker_list;
            intptr_t arg2 = (intptr_t)dcb;

            if (!worker->post_message(MXS_WORKER_MSG_CALL, arg1, arg2))
            {
                MXS_ERROR("Could not post DCB to worker.");
            }
        }
    }
}

/**
 * Remove a DCB from the owner's list
 *
 * @param dcb DCB to remove
 */
static void dcb_remove_from_list(DCB *dcb)
{
    if (dcb == this_unit.all_dcbs[dcb->poll.thread.id])
    {
        DCB *tail = this_unit.all_dcbs[dcb->poll.thread.id]->thread.tail;
        this_unit.all_dcbs[dcb->poll.thread.id] = this_unit.all_dcbs[dcb->poll.thread.id]->thread.next;

        if (this_unit.all_dcbs[dcb->poll.thread.id])
        {
            this_unit.all_dcbs[dcb->poll.thread.id]->thread.tail = tail;
        }
    }
    else
    {
        DCB *current = this_unit.all_dcbs[dcb->poll.thread.id]->thread.next;
        DCB *prev = this_unit.all_dcbs[dcb->poll.thread.id];

        while (current)
        {
            if (current == dcb)
            {
                if (current == this_unit.all_dcbs[dcb->poll.thread.id]->thread.tail)
                {
                    this_unit.all_dcbs[dcb->poll.thread.id]->thread.tail = prev;
                }
                prev->thread.next = current->thread.next;
                break;
            }
            prev = current;
            current = current->thread.next;
        }
    }

    /** Reset the next and tail pointers so that if this DCB is added to the list
     * again, it will be in a clean state. */
    dcb->thread.next = NULL;
    dcb->thread.tail = NULL;
}

/**
 * Enable the timing out of idle connections.
 */
void dcb_enable_session_timeouts()
{
    this_unit.check_timeouts = true;
}

/**
 * Close sessions that have been idle for too long.
 *
 * If the time since a session last sent data is greater than the set value in the
 * service, it is disconnected. The connection timeout is disabled by default.
 */
void dcb_process_idle_sessions(int thr)
{
    if (this_unit.check_timeouts && hkheartbeat >= this_thread.next_timeout_check)
    {
        /** Because the resolution of the timeout is one second, we only need to
         * check for it once per second. One heartbeat is 100 milliseconds. */
        this_thread.next_timeout_check = hkheartbeat + 10;

        for (DCB *dcb = this_unit.all_dcbs[thr]; dcb; dcb = dcb->thread.next)
        {
            if (dcb->dcb_role == DCB_ROLE_CLIENT_HANDLER)
            {
                ss_dassert(dcb->listener);
                SERVICE *service = dcb->listener->service;

                if (service->conn_idle_timeout && dcb->state == DCB_STATE_POLLING)
                {
                    int64_t idle = hkheartbeat - dcb->last_read;
                    int64_t timeout = service->conn_idle_timeout * 10;

                    if (idle > timeout)
                    {
                        MXS_WARNING("Timing out '%s'@%s, idle for %.1f seconds",
                                    dcb->user ? dcb->user : "<unknown>",
                                    dcb->remote ? dcb->remote : "<unknown>",
                                    (float)idle / 10.f);
                        poll_fake_hangup_event(dcb);
                    }
                }
            }
        }
    }
}

/** Helper class for serial iteration over all DCBs */
class SerialDcbTask : public WorkerTask
{
public:

    SerialDcbTask(bool(*func)(DCB *, void *), void *data):
        m_func(func),
        m_data(data),
        m_more(1)
    {
    }

    void execute(Worker& worker)
    {
        int thread_id = worker.id();

        for (DCB *dcb = this_unit.all_dcbs[thread_id]; dcb && atomic_load_int32(&m_more); dcb = dcb->thread.next)
        {
            if (!m_func(dcb, m_data))
            {
                atomic_store_int32(&m_more, 0);
                break;
            }
        }
    }

    bool more() const
    {
        return m_more;
    }

private:
    bool(*m_func)(DCB *dcb, void *data);
    void* m_data;
    int m_more;
};

bool dcb_foreach(bool(*func)(DCB *dcb, void *data), void *data)
{
    SerialDcbTask task(func, data);
    Worker::execute_serially(task);
    return task.more();
}

/** Helper class for parallel iteration over all DCBs */
class ParallelDcbTask : public WorkerTask
{
public:

    ParallelDcbTask(bool(*func)(DCB *, void *), void **data):
        m_func(func),
        m_data(data)
    {
    }

    void execute(Worker& worker)
    {
        int thread_id = worker.id();

        for (DCB *dcb = this_unit.all_dcbs[thread_id]; dcb; dcb = dcb->thread.next)
        {
            if (!m_func(dcb, m_data[thread_id]))
            {
                break;
            }
        }
    }

private:
    bool(*m_func)(DCB *dcb, void *data);
    void** m_data;
};

void dcb_foreach_parallel(bool(*func)(DCB *dcb, void *data), void **data)
{
    ParallelDcbTask task(func, data);
    Worker::execute_concurrently(task);
}

int dcb_get_port(const DCB *dcb)
{
    int rval = -1;

    if (dcb->ip.ss_family == AF_INET)
    {
        struct sockaddr_in* ip = (struct sockaddr_in*)&dcb->ip;
        rval = ntohs(ip->sin_port);
    }
    else if (dcb->ip.ss_family == AF_INET6)
    {
        struct sockaddr_in6* ip = (struct sockaddr_in6*)&dcb->ip;
        rval = ntohs(ip->sin6_port);
    }
    else
    {
        ss_dassert(dcb->ip.ss_family == AF_UNIX);
    }

    return rval;
}

static uint32_t dcb_process_poll_events(DCB *dcb, uint32_t events)
{
    ss_dassert(dcb->poll.thread.id == mxs::Worker::get_current_id() ||
               dcb->dcb_role == DCB_ROLE_SERVICE_LISTENER);

    CHK_DCB(dcb);

    uint32_t rc = MXS_POLL_NOP;

    /* It isn't obvious that this is impossible */
    /* ss_dassert(dcb->state != DCB_STATE_DISCONNECTED); */
    if (DCB_STATE_DISCONNECTED == dcb->state)
    {
        return rc;
    }

    MXS_DEBUG("%lu [poll_waitevents] event %d dcb %p "
              "role %s",
              pthread_self(),
              events,
              dcb,
              STRDCBROLE(dcb->dcb_role));

    if (dcb->n_close != 0)
    {
        MXS_WARNING("Events reported for dcb(%p), owned by %d, that has been closed %" PRIu32 " times.",
                    dcb, dcb->poll.thread.id, dcb->n_close);
    }

    if ((events & EPOLLOUT) && (dcb->n_close == 0))
    {
        int eno = 0;
        eno = gw_getsockerrno(dcb->fd);

        if (eno == 0)
        {
            rc |= MXS_POLL_WRITE;

            if (dcb_session_check(dcb, "write_ready"))
            {
                DCB_EH_NOTICE("Calling dcb->func.write_ready(%p)", dcb);
                dcb->func.write_ready(dcb);
            }
        }
        else
        {
            char errbuf[MXS_STRERROR_BUFLEN];
            MXS_DEBUG("%lu [poll_waitevents] "
                      "EPOLLOUT due %d, %s. "
                      "dcb %p, fd %i",
                      pthread_self(),
                      eno,
                      strerror_r(eno, errbuf, sizeof(errbuf)),
                      dcb,
                      dcb->fd);
        }
    }
    if ((events & EPOLLIN) && (dcb->n_close == 0))
    {
        if (dcb->state == DCB_STATE_LISTENING || dcb->state == DCB_STATE_WAITING)
        {
            MXS_DEBUG("%lu [poll_waitevents] "
                      "Accept in fd %d",
                      pthread_self(),
                      dcb->fd);
            rc |= MXS_POLL_ACCEPT;

            if (dcb_session_check(dcb, "accept"))
            {
                DCB_EH_NOTICE("Calling dcb->func.accept(%p)", dcb);
                dcb->func.accept(dcb);
            }
        }
        else
        {
            MXS_DEBUG("%lu [poll_waitevents] "
                      "Read in dcb %p fd %d",
                      pthread_self(),
                      dcb,
                      dcb->fd);
            rc |= MXS_POLL_READ;

            if (dcb_session_check(dcb, "read"))
            {
                int return_code = 1;
                /** SSL authentication is still going on, we need to call dcb_accept_SSL
                 * until it return 1 for success or -1 for error */
                if (dcb->ssl_state == SSL_HANDSHAKE_REQUIRED)
                {
                    return_code = (DCB_ROLE_CLIENT_HANDLER == dcb->dcb_role) ?
                                  dcb_accept_SSL(dcb) :
                                  dcb_connect_SSL(dcb);
                }
                if (1 == return_code)
                {
                    DCB_EH_NOTICE("Calling dcb->func.read(%p)", dcb);
                    dcb->func.read(dcb);
                }
            }
        }
    }
    if ((events & EPOLLERR)  && (dcb->n_close == 0))
    {
        int eno = gw_getsockerrno(dcb->fd);
        if (eno != 0)
        {
            char errbuf[MXS_STRERROR_BUFLEN];
            MXS_DEBUG("%lu [poll_waitevents] "
                      "EPOLLERR due %d, %s.",
                      pthread_self(),
                      eno,
                      strerror_r(eno, errbuf, sizeof(errbuf)));
        }
        rc |= MXS_POLL_ERROR;

        if (dcb_session_check(dcb, "error"))
        {
            DCB_EH_NOTICE("Calling dcb->func.error(%p)", dcb);
            dcb->func.error(dcb);
        }
    }

    if ((events & EPOLLHUP) && (dcb->n_close == 0))
    {
        ss_debug(int eno = gw_getsockerrno(dcb->fd));
        ss_debug(char errbuf[MXS_STRERROR_BUFLEN]);
        MXS_DEBUG("%lu [poll_waitevents] "
                  "EPOLLHUP on dcb %p, fd %d. "
                  "Errno %d, %s.",
                  pthread_self(),
                  dcb,
                  dcb->fd,
                  eno,
                  strerror_r(eno, errbuf, sizeof(errbuf)));
        rc |= MXS_POLL_HUP;
        if ((dcb->flags & DCBF_HUNG) == 0)
        {
            dcb->flags |= DCBF_HUNG;

            if (dcb_session_check(dcb, "hangup EPOLLHUP"))
            {
                DCB_EH_NOTICE("Calling dcb->func.hangup(%p)", dcb);
                dcb->func.hangup(dcb);
            }
        }
    }

#ifdef EPOLLRDHUP
    if ((events & EPOLLRDHUP) && (dcb->n_close == 0))
    {
        ss_debug(int eno = gw_getsockerrno(dcb->fd));
        ss_debug(char errbuf[MXS_STRERROR_BUFLEN]);
        MXS_DEBUG("%lu [poll_waitevents] "
                  "EPOLLRDHUP on dcb %p, fd %d. "
                  "Errno %d, %s.",
                  pthread_self(),
                  dcb,
                  dcb->fd,
                  eno,
                  strerror_r(eno, errbuf, sizeof(errbuf)));
        rc |= MXS_POLL_HUP;

        if ((dcb->flags & DCBF_HUNG) == 0)
        {
            dcb->flags |= DCBF_HUNG;

            if (dcb_session_check(dcb, "hangup EPOLLRDHUP"))
            {
                DCB_EH_NOTICE("Calling dcb->func.hangup(%p)", dcb);
                dcb->func.hangup(dcb);
            }
        }
    }
#endif

    if (dcb->n_close != 0)
    {
        dcb_final_close(dcb);
    }

    return rc;
}

static uint32_t dcb_handler(DCB* dcb, uint32_t events)
{
    this_thread.current_dcb = dcb;
    uint32_t rv = dcb_process_poll_events(dcb, events);
    this_thread.current_dcb = NULL;

    return rv;
}

static uint32_t dcb_poll_handler(MXS_POLL_DATA *data, int thread_id, uint32_t events)
{
    DCB *dcb = (DCB*)data;

    return dcb_handler(dcb, events);
}

class FakeEventTask: public mxs::WorkerDisposableTask
{
    FakeEventTask(const FakeEventTask&);
    FakeEventTask& operator=(const FakeEventTask&);

public:
    FakeEventTask(DCB* dcb, GWBUF* buf, uint32_t ev):
        m_dcb(dcb),
        m_buffer(buf),
        m_ev(ev)
    {
    }

    void execute(Worker& worker)
    {
        m_dcb->dcb_fakequeue = m_buffer;
        dcb_handler(m_dcb, m_ev);
    }

private:
    DCB*     m_dcb;
    GWBUF*   m_buffer;
    uint32_t m_ev;
};

static void poll_add_event_to_dcb(DCB* dcb, GWBUF* buf, uint32_t ev)
{
    FakeEventTask* task = new (std::nothrow) FakeEventTask(dcb, buf, ev);

    if (task)
    {
        Worker* worker = Worker::get(dcb->poll.thread.id);
        worker->post(std::auto_ptr<FakeEventTask>(task), mxs::Worker::EXECUTE_QUEUED);
    }
    else
    {
        MXS_OOM();
    }
}

void poll_add_epollin_event_to_dcb(DCB* dcb, GWBUF* buf)
{
    poll_add_event_to_dcb(dcb, buf, EPOLLIN);
}

void poll_fake_write_event(DCB *dcb)
{
    poll_add_event_to_dcb(dcb, NULL, EPOLLOUT);
}

void poll_fake_read_event(DCB *dcb)
{
    poll_add_event_to_dcb(dcb, NULL, EPOLLIN);
}

void poll_fake_hangup_event(DCB *dcb)
{
#ifdef EPOLLRDHUP
    uint32_t ev = EPOLLRDHUP;
#else
    uint32_t ev = EPOLLHUP;
#endif
    poll_add_event_to_dcb(dcb, NULL, ev);
}

/**
 * Check that the DCB has a session link before processing.
 * If not, log an error.  Processing will be bypassed
 *
 * @param   dcb         The DCB to check
 * @param   function    The name of the function about to be called
 * @return  bool        Does the DCB have a non-null session link
 */
static bool
dcb_session_check(DCB *dcb, const char *function)
{
    if (dcb->session)
    {
        return true;
    }
    else
    {
        MXS_ERROR("%lu [%s] The dcb %p that was about to be processed by %s does not "
                  "have a non-null session pointer ",
                  pthread_self(),
                  __func__,
                  dcb,
                  function);
        return false;
    }
}

int poll_add_dcb(DCB *dcb)
{
    int rc = -1;
    dcb_state_t old_state = dcb->state;
    dcb_state_t new_state;
    uint32_t events = 0;

    CHK_DCB(dcb);

#ifdef EPOLLRDHUP
    events = EPOLLIN | EPOLLOUT | EPOLLRDHUP | EPOLLHUP | EPOLLET;
#else
    events = EPOLLIN | EPOLLOUT | EPOLLHUP | EPOLLET;
#endif

    /*<
     * Choose new state according to the role of dcb.
     */
    if (dcb->dcb_role == DCB_ROLE_CLIENT_HANDLER || dcb->dcb_role == DCB_ROLE_BACKEND_HANDLER)
    {
        new_state = DCB_STATE_POLLING;
    }
    else
    {
        /**
         * Listeners are always added in level triggered mode. This will cause
         * new events to be generated as long as there is at least one connection
         * to accept.
         */
        events = EPOLLIN;
        ss_dassert(dcb->dcb_role == DCB_ROLE_SERVICE_LISTENER);
        new_state = DCB_STATE_LISTENING;
    }
    /*
     * Check DCB current state seems sensible
     */
    if (DCB_STATE_DISCONNECTED == dcb->state
        || DCB_STATE_UNDEFINED == dcb->state)
    {
        MXS_ERROR("%lu [poll_add_dcb] Error : existing state of dcb %p "
                  "is %s, but this should be impossible, crashing.",
                  pthread_self(),
                  dcb,
                  STRDCBSTATE(dcb->state));
        raise(SIGABRT);
    }
    if (DCB_STATE_POLLING == dcb->state
        || DCB_STATE_LISTENING == dcb->state)
    {
        MXS_ERROR("%lu [poll_add_dcb] Error : existing state of dcb %p "
                  "is %s, but this is probably an error, not crashing.",
                  pthread_self(),
                  dcb,
                  STRDCBSTATE(dcb->state));
    }
    dcb->state = new_state;

    /*
     * The only possible failure that will not cause a crash is
     * running out of system resources.
     */
    int worker_id = 0;

    if (dcb->dcb_role == DCB_ROLE_SERVICE_LISTENER)
    {
        worker_id = MXS_WORKER_ALL;
    }
    else if (dcb->dcb_role == DCB_ROLE_BACKEND_HANDLER)
    {
        ss_dassert(Worker::get_current_id() != -1);

        worker_id = dcb->session->client_dcb->poll.thread.id;
        ss_dassert(worker_id == Worker::get_current_id());
    }
    else
    {
        ss_dassert(Worker::get_current_id() != -1);

        worker_id = Worker::get_current_id();
    }

    if (poll_add_fd_to_worker(worker_id, dcb->fd, events, (MXS_POLL_DATA*)dcb))
    {
        dcb_add_to_list(dcb);

        MXS_DEBUG("%lu [poll_add_dcb] Added dcb %p in state %s to poll set.",
                  pthread_self(),
                  dcb,
                  STRDCBSTATE(dcb->state));
        rc = 0;
    }
    else
    {
        dcb->state = old_state;
        rc = -1;
    }
    return rc;
}

int poll_remove_dcb(DCB *dcb)
{
    int dcbfd, rc = 0;
    struct  epoll_event ev;
    CHK_DCB(dcb);

    /*< It is possible that dcb has already been removed from the set */
    if (dcb->state == DCB_STATE_NOPOLLING)
    {
        return 0;
    }
    if (DCB_STATE_POLLING != dcb->state
        && DCB_STATE_LISTENING != dcb->state)
    {
        MXS_ERROR("%lu [poll_remove_dcb] Error : existing state of dcb %p "
                  "is %s, but this is probably an error, not crashing.",
                  pthread_self(),
                  dcb,
                  STRDCBSTATE(dcb->state));
    }
    /*<
     * Set state to NOPOLLING and remove dcb from poll set.
     */
    dcb->state = DCB_STATE_NOPOLLING;

    /**
     * Only positive fds can be removed from epoll set.
     */
    dcbfd = dcb->fd;
    ss_dassert(dcbfd > 0 || dcb->dcb_role == DCB_ROLE_INTERNAL);

    if (dcbfd > 0)
    {
        int worker_id;

        if (dcb->dcb_role == DCB_ROLE_SERVICE_LISTENER)
        {
            worker_id = MXS_WORKER_ALL;
        }
        else
        {
            worker_id = dcb->poll.thread.id;
        }

        if (poll_remove_fd_from_worker(worker_id, dcbfd))
        {
            rc = 0;
        }
        else
        {
            rc = -1;
        }
    }
    return rc;
}

DCB* dcb_get_current()
{
    return this_thread.current_dcb;
}
