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
 * @file dcb.c  -  Descriptor Control Block generic functions
 *
 * Descriptor control blocks provide the key mechanism for the interface
 * with the non-blocking socket polling routines. The descriptor control
 * block is the user data that is handled by the epoll system and contains
 * the state data and pointers to other components that relate to the
 * use of a file descriptor.
 */
#include "internal/dcb.hh"

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
#include <maxbase/atomic.h>
#include <maxbase/atomic.hh>
#include <maxscale/clock.h>
#include <maxscale/limits.h>
#include <maxscale/listener.hh>
#include <maxscale/poll.hh>
#include <maxscale/router.hh>
#include <maxscale/service.hh>
#include <maxscale/utils.h>
#include <maxscale/routingworker.hh>

#include "internal/modules.hh"
#include "internal/server.hh"
#include "internal/session.hh"

using maxscale::RoutingWorker;
using maxbase::Worker;
using std::string;

// #define DCB_LOG_EVENT_HANDLING
#if defined (DCB_LOG_EVENT_HANDLING)
#define DCB_EH_NOTICE(s, p) MXS_NOTICE(s, p)
#else
#define DCB_EH_NOTICE(s, p)
#endif

#ifdef EPOLLRDHUP
constexpr uint32_t poll_events = EPOLLIN | EPOLLOUT | EPOLLRDHUP | EPOLLHUP | EPOLLET;
#else
constexpr uint32_t poll_events = EPOLLIN | EPOLLOUT | EPOLLHUP | EPOLLET;
#endif

namespace
{

static struct
{
    DCB** all_dcbs;         /** #workers sized array of pointers to DCBs where dcbs are listed. */
    bool  check_timeouts;   /** Should session timeouts be checked. */
} this_unit;

static thread_local struct
{
    long next_timeout_check;/** When to next check for idle sessions. */
    DCB* current_dcb;       /** The DCB currently being handled by event handlers. */
} this_thread;
}

static void        dcb_initialize(DCB* dcb);
static void        dcb_final_free(DCB* dcb);
static void        dcb_call_callback(DCB* dcb, DCB_REASON reason);
static int         dcb_null_write(DCB* dcb, GWBUF* buf);
static int         dcb_null_auth(DCB* dcb, SERVER* server, MXS_SESSION* session, GWBUF* buf);
static inline DCB* dcb_find_in_list(DCB* dcb);
static void        dcb_stop_polling_and_shutdown(DCB* dcb);
static bool        dcb_maybe_add_persistent(DCB*);
static inline bool dcb_write_parameter_check(DCB* dcb, GWBUF* queue);
static int         dcb_bytes_readable(DCB* dcb);
static int         dcb_read_no_bytes_available(DCB* dcb, int nreadtotal);
static int         dcb_create_SSL(DCB* dcb, SSL_LISTENER* ssl);
static int         dcb_read_SSL(DCB* dcb, GWBUF** head);
static GWBUF*      dcb_basic_read(DCB* dcb,
                                  int bytesavailable,
                                  int maxbytes,
                                  int nreadtotal,
                                  int* nsingleread);
static GWBUF* dcb_basic_read_SSL(DCB* dcb, int* nsingleread);
static void   dcb_log_write_failure(DCB* dcb, GWBUF* queue, int eno);
static int    gw_write(DCB* dcb, GWBUF* writeq, bool* stop_writing);
static int    gw_write_SSL(DCB* dcb, GWBUF* writeq, bool* stop_writing);
static int    dcb_log_errors_SSL(DCB* dcb, int ret);
static int    dcb_listen_create_socket_inet(const char* host, uint16_t port);
static int    dcb_listen_create_socket_unix(const char* path);
static int    dcb_set_socket_option(int sockfd, int level, int optname, void* optval, socklen_t optlen);
static void   dcb_add_to_all_list(DCB* dcb);
static void   dcb_add_to_list(DCB* dcb);
static bool   dcb_add_to_worker(Worker* worker, DCB* dcb, uint32_t events);
static DCB*   dcb_find_free();
static void   dcb_remove_from_list(DCB* dcb);

static uint32_t dcb_poll_handler(MXB_POLL_DATA* data, MXB_WORKER* worker, uint32_t events);
static uint32_t dcb_process_poll_events(DCB* dcb, uint32_t ev);
static bool     dcb_session_check(DCB* dcb, const char*);
static int      upstream_throttle_callback(DCB* dcb, DCB_REASON reason, void* userdata);
static int      downstream_throttle_callback(DCB* dcb, DCB_REASON reason, void* userdata);

void dcb_global_init()
{
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

uint64_t dcb_get_session_id(DCB* dcb)
{
    return (dcb && dcb->session) ? dcb->session->ses_id : 0;
}

static MXB_WORKER* get_dcb_owner()
{
    /** The DCB is owned by the thread that allocates it */
    mxb_assert(RoutingWorker::get_current_id() != -1);
    return RoutingWorker::get_current();
}

DCB::DCB(Role role, MXS_SESSION* session)
    : MXB_POLL_DATA{dcb_poll_handler, get_dcb_owner()}
    , role(role)
    , session(session)
    , high_water(config_writeq_high_water())
    , low_water(config_writeq_low_water())
    , service(session->service)
    , last_read(mxs_clock())
{
    // TODO: Remove DCB::Role::INTERNAL to always have a valid listener
    if (session->listener)
    {
        func = session->listener->protocol_func();
        authfunc = session->listener->auth_func();
    }

    if (high_water && low_water)
    {
        dcb_add_callback(this, DCB_REASON_HIGH_WATER, downstream_throttle_callback, NULL);
        dcb_add_callback(this, DCB_REASON_LOW_WATER, downstream_throttle_callback, NULL);
    }
}

DCB::~DCB()
{
    if (data && authfunc.free)
    {
        authfunc.free(this);
    }

    if (authfunc.destroy)
    {
        authfunc.destroy(authenticator_data);
    }

    while (callbacks)
    {
        DCB_CALLBACK* tmp = callbacks;
        callbacks = callbacks->next;
        MXS_FREE(tmp);
    }

    if (ssl)
    {
        SSL_free(ssl);
    }

    MXS_FREE(remote);
    MXS_FREE(user);
    MXS_FREE(protocol);
    gwbuf_free(delayq);
    gwbuf_free(writeq);
    gwbuf_free(readq);
    gwbuf_free(fakeq);

    owner = reinterpret_cast<MXB_WORKER*>(0xdeadbeef);
}

/**
 * @brief Allocate a new DCB.
 *
 * This routine performs the generic initialisation on the DCB before returning
 * the newly allocated DCB.
 *
 * Remaining fields are set from the given parameters, and then the DCB is
 * flagged as ready for use.
 *
 * @param role     The role for the new DCB
 * @param listener The listener if applicable.
 * @param service  The service which is used
 *
 * @return An available DCB or NULL if none could be allocated.
 */
DCB* dcb_alloc(DCB::Role role, MXS_SESSION* session)
{
    return new(std::nothrow) DCB(role, session);
}

/**
 * Free a DCB and remove it from the chain of all DCBs
 *
 * @param dcb The DCB to free
 */
static void dcb_final_free(DCB* dcb)
{
    mxb_assert_message(dcb->state == DCB_STATE_DISCONNECTED || dcb->state == DCB_STATE_ALLOC,
                       "dcb not in DCB_STATE_DISCONNECTED not in DCB_STATE_ALLOC state.");

    if (dcb->session)
    {
        /*<
         * Terminate client session.
         */
        MXS_SESSION* local_session = dcb->session;
        dcb->session = NULL;
        if (dcb->role == DCB::Role::BACKEND)
        {
            session_unlink_backend_dcb(local_session, dcb);
        }
        else
        {
            /**
             * The client DCB is only freed once all other DCBs that the session
             * uses have been freed. This will guarantee that the authentication
             * data will be usable for all DCBs even if the client DCB has already
             * been closed.
             */

            mxb_assert(dcb->role == DCB::Role::CLIENT || dcb->role == DCB::Role::INTERNAL);
            session_put_ref(local_session);
            return;
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
void dcb_free_all_memory(DCB* dcb)
{
    // This needs to be done here because session_free() calls this directly.
    if (this_thread.current_dcb == dcb)
    {
        this_thread.current_dcb = NULL;
    }

    delete dcb;
}

/**
 * Remove a DCB from the poll list and trigger shutdown mechanisms.
 *
 * @param       dcb     The DCB to be processed
 */
static void dcb_stop_polling_and_shutdown(DCB* dcb)
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
 * @param srv           The server to connect to
 * @param session       The session this connection is being made for
 * @param protocol      The protocol module to use
 * @return              The new allocated dcb or NULL if the DCB was not connected
 */
DCB* dcb_connect(SERVER* srv, MXS_SESSION* session, const char* protocol)
{
    DCB* dcb;
    MXS_PROTOCOL* funcs;
    int fd;
    int rc;
    const char* user;
    Server* server = static_cast<Server*>(srv);
    user = session_get_user(session);
    if (user && strlen(user))
    {
        MXS_DEBUG("Looking for persistent connection DCB user %s protocol %s", user, protocol);
        dcb = server->get_persistent_dcb(user, session->client_dcb->remote, protocol,
                                         static_cast<RoutingWorker*>(session->client_dcb->owner)->id());
        if (dcb)
        {
            /**
             * Link dcb to session. Unlink is called in dcb_final_free
             */
            session_link_backend_dcb(session, dcb);

            MXS_DEBUG("Reusing a persistent connection, dcb %p", dcb);
            dcb->persistentstart = 0;
            dcb->was_persistent = true;
            dcb->last_read = mxs_clock();
            mxb::atomic::add(&server->stats.n_from_pool, 1, mxb::atomic::RELAXED);
            return dcb;
        }
        else
        {
            MXS_DEBUG("Failed to find a reusable persistent connection");
        }
    }

    if ((dcb = dcb_alloc(DCB::Role::BACKEND, session)) == NULL)
    {
        return NULL;
    }

    if ((funcs = (MXS_PROTOCOL*)load_module(protocol,
                                            MODULE_PROTOCOL)) == NULL)
    {
        dcb->state = DCB_STATE_DISCONNECTED;
        dcb_free_all_memory(dcb);
        MXS_ERROR("Failed to load protocol module '%s'", protocol);
        return NULL;
    }
    memcpy(&(dcb->func), funcs, sizeof(MXS_PROTOCOL));

    if (session->client_dcb->remote)
    {
        dcb->remote = MXS_STRDUP_A(session->client_dcb->remote);
    }

    string authenticator = server->get_authenticator();
    if (authenticator.empty())
    {
        if (dcb->func.auth_default)
        {
            authenticator = dcb->func.auth_default();
        }
        else
        {
            authenticator = "NullAuthDeny";
        }
    }

    MXS_AUTHENTICATOR* authfuncs = (MXS_AUTHENTICATOR*)load_module(authenticator.c_str(),
                                                                   MODULE_AUTHENTICATOR);
    if (authfuncs == NULL)
    {
        MXS_ERROR("Failed to load authenticator module '%s'", authenticator.c_str());
        dcb_free_all_memory(dcb);
        return NULL;
    }

    memcpy(&dcb->authfunc, authfuncs, sizeof(MXS_AUTHENTICATOR));

    /**
     * Link dcb to session. Unlink is called in dcb_final_free
     */
    session_link_backend_dcb(session, dcb);

    fd = dcb->func.connect(dcb, server, session);

    if (fd == DCBFD_CLOSED)
    {
        MXS_DEBUG("Failed to connect to server [%s]:%d, from backend dcb %p, client dcp %p fd %d",
                  server->address,
                  server->port,
                  dcb,
                  session->client_dcb,
                  session->client_dcb->fd);
        // Remove the inc ref that was done in session_link_backend_dcb().
        session_unlink_backend_dcb(dcb->session, dcb);
        dcb->session = NULL;
        dcb_free_all_memory(dcb);
        return NULL;
    }
    else
    {
        MXS_DEBUG("Connected to server [%s]:%d, from backend dcb %p, client dcp %p fd %d.",
                  server->address,
                  server->port,
                  dcb,
                  session->client_dcb,
                  session->client_dcb->fd);
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
    auto auth_create = dcb->authfunc.create;
    if (auth_create)
    {
        Server* server = static_cast<Server*>(dcb->server);
        if ((dcb->authenticator_data = auth_create(server->auth_instance())) == NULL)
        {
            MXS_ERROR("Failed to create authenticator for backend DCB.");
            close(dcb->fd);
            dcb->fd = DCBFD_CLOSED;
            // Remove the inc ref that was done in session_link_backend_dcb().
            session_unlink_backend_dcb(dcb->session, dcb);
            dcb->session = NULL;
            dcb_free_all_memory(dcb);
            return NULL;
        }
    }

    /**
     * Add the dcb in the poll set
     */
    rc = poll_add_dcb(dcb);

    if (rc != 0)
    {
        close(dcb->fd);
        dcb->fd = DCBFD_CLOSED;
        // Remove the inc ref that was done in session_link_backend_dcb().
        session_unlink_backend_dcb(dcb->session, dcb);
        dcb->session = NULL;
        dcb_free_all_memory(dcb);
        return NULL;
    }

    /* Register upstream throttling callbacks */
    if (DCB_THROTTLING_ENABLED(dcb))
    {
        dcb_add_callback(dcb, DCB_REASON_HIGH_WATER, upstream_throttle_callback, NULL);
        dcb_add_callback(dcb, DCB_REASON_LOW_WATER, upstream_throttle_callback, NULL);
    }
    /**
     * The dcb will be addded into poll set by dcb->func.connect
     */
    mxb::atomic::add(&server->stats.n_connections, 1, mxb::atomic::RELAXED);
    mxb::atomic::add(&server->stats.n_current, 1, mxb::atomic::RELAXED);

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
int dcb_read(DCB* dcb,
             GWBUF** head,
             int maxbytes)
{
    int nsingleread = 0;
    int nreadtotal = 0;

    if (dcb->readq)
    {
        *head = gwbuf_append(*head, dcb->readq);
        dcb->readq = NULL;
        nreadtotal = gwbuf_length(*head);
    }
    else if (dcb->fakeq)
    {
        *head = gwbuf_append(*head, dcb->fakeq);
        dcb->fakeq = NULL;
        nreadtotal = gwbuf_length(*head);
    }

    if (SSL_HANDSHAKE_DONE == dcb->ssl_state || SSL_ESTABLISHED == dcb->ssl_state)
    {
        return dcb_read_SSL(dcb, head);
    }

    if (dcb->fd == DCBFD_CLOSED)
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
            return bytes_available < 0 ? -1
                                       :/** Handle closed client socket */
                   dcb_read_no_bytes_available(dcb, nreadtotal);
        }
        else
        {
            GWBUF* buffer;
            dcb->last_read = mxs_clock();

            buffer = dcb_basic_read(dcb, bytes_available, maxbytes, nreadtotal, &nsingleread);
            if (buffer)
            {
                nreadtotal += nsingleread;
                MXS_DEBUG("Read %d bytes from dcb %p in state %s fd %d.",
                          nsingleread,
                          dcb,
                          STRDCBSTATE(dcb->state),
                          dcb->fd);

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
    }   /*< while (0 == maxbytes || nreadtotal < maxbytes) */

    return nreadtotal;
}

/**
 * Find the number of bytes available for the DCB's socket
 *
 * @param dcb       The DCB to read from
 * @return          -1 on error, otherwise the total number of bytes available
 */
static int dcb_bytes_readable(DCB* dcb)
{
    int bytesavailable;

    if (-1 == ioctl(dcb->fd, FIONREAD, &bytesavailable))
    {
        MXS_ERROR("ioctl FIONREAD for dcb %p in state %s fd %d failed: %d, %s",
                  dcb,
                  STRDCBSTATE(dcb->state),
                  dcb->fd,
                  errno,
                  mxs_strerror(errno));
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
static int dcb_read_no_bytes_available(DCB* dcb, int nreadtotal)
{
    /** Handle closed client socket */
    if (nreadtotal == 0 && DCB::Role::CLIENT == dcb->role)
    {
        char c;
        int l_errno = 0;
        long r = -1;

        /* try to read 1 byte, without consuming the socket buffer */
        r = recv(dcb->fd, &c, sizeof(char), MSG_PEEK);
        l_errno = errno;

        if (r <= 0
            && l_errno != EAGAIN
            && l_errno != EWOULDBLOCK
            && l_errno != 0)
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
static GWBUF* dcb_basic_read(DCB* dcb, int bytesavailable, int maxbytes, int nreadtotal, int* nsingleread)
{
    GWBUF* buffer;
    int bufsize = maxbytes == 0 ? bytesavailable : MXS_MIN(bytesavailable, maxbytes - nreadtotal);

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
                          dcb,
                          STRDCBSTATE(dcb->state),
                          dcb->fd,
                          errno,
                          mxs_strerror(errno));
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
static int dcb_read_SSL(DCB* dcb, GWBUF** head)
{
    GWBUF* buffer;
    int nsingleread = 0, nreadtotal = 0;
    int start_length = gwbuf_length(*head);

    if (dcb->fd == DCBFD_CLOSED)
    {
        MXS_ERROR("Read failed, dcb is closed.");
        return -1;
    }

    if (dcb->ssl_write_want_read)
    {
        dcb_drain_writeq(dcb);
    }

    dcb->last_read = mxs_clock();
    buffer = dcb_basic_read_SSL(dcb, &nsingleread);
    if (buffer)
    {
        nreadtotal += nsingleread;
        *head = gwbuf_append(*head, buffer);

        while (buffer)
        {
            dcb->last_read = mxs_clock();
            buffer = dcb_basic_read_SSL(dcb, &nsingleread);
            if (buffer)
            {
                nreadtotal += nsingleread;
                /*< Append read data to the gwbuf */
                *head = gwbuf_append(*head, buffer);
            }
        }
    }

    mxb_assert(gwbuf_length(*head) == (size_t)(start_length + nreadtotal));

    return nsingleread < 0 ? nsingleread : nreadtotal;
}

/**
 * Basic read function to carry out a single read on the DCB's SSL connection
 *
 * @param dcb           The DCB to read from
 * @param nsingleread   To be set as the number of bytes read this time
 * @return              GWBUF* buffer containing the data, or null.
 */
static GWBUF* dcb_basic_read_SSL(DCB* dcb, int* nsingleread)
{
    unsigned char temp_buffer[MXS_SO_RCVBUF_SIZE];
    GWBUF* buffer = NULL;

    *nsingleread = SSL_read(dcb->ssl, temp_buffer, MXS_SO_RCVBUF_SIZE);

    dcb->stats.n_reads++;

    switch (SSL_get_error(dcb->ssl, *nsingleread))
    {
    case SSL_ERROR_NONE:
        /* Successful read */
        if (*nsingleread && (buffer = gwbuf_alloc_and_load(*nsingleread, (void*)temp_buffer)) == NULL)
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
        poll_fake_hangup_event(dcb);
        *nsingleread = 0;
        break;

    case SSL_ERROR_WANT_READ:
        /* Prevent SSL I/O on connection until retried, return to poll loop */
        dcb->ssl_read_want_write = false;
        dcb->ssl_read_want_read = true;
        *nsingleread = 0;
        break;

    case SSL_ERROR_WANT_WRITE:
        /* Prevent SSL I/O on connection until retried, return to poll loop */
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
static int dcb_log_errors_SSL(DCB* dcb, int ret)
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
                  dcb,
                  STRDCBSTATE(dcb->state),
                  dcb->fd,
                  ret);
    }
    if (ret && !ssl_errno)
    {
        int local_errno = errno;
        MXS_ERROR("SSL error caused by TCP error %d %s",
                  local_errno,
                  mxs_strerror(local_errno));
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
int dcb_write(DCB* dcb, GWBUF* queue)
{
    dcb->writeqlen += gwbuf_length(queue);
    // The following guarantees that queue is not NULL
    if (!dcb_write_parameter_check(dcb, queue))
    {
        return 0;
    }

    dcb->writeq = gwbuf_append(dcb->writeq, queue);
    dcb->stats.n_buffered++;
    dcb_drain_writeq(dcb);

    if (DCB_ABOVE_HIGH_WATER(dcb) && !dcb->high_water_reached)
    {
        dcb_call_callback(dcb, DCB_REASON_HIGH_WATER);
        dcb->high_water_reached = true;
        dcb->stats.n_high_water++;
    }

    return 1;
}

/**
 * Check the parameters for dcb_write
 *
 * @param dcb   The DCB of the client
 * @param queue Queue of buffers to write
 * @return true if parameters acceptable, false otherwise
 */
static inline bool dcb_write_parameter_check(DCB* dcb, GWBUF* queue)
{
    if (queue == NULL)
    {
        return false;
    }

    if (dcb->fd == DCBFD_CLOSED)
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
        if (dcb->state != DCB_STATE_ALLOC
            && dcb->state != DCB_STATE_POLLING
            && dcb->state != DCB_STATE_LISTENING
            && dcb->state != DCB_STATE_NOPOLLING)
        {
            MXS_DEBUG("Write aborted to dcb %p because it is in state %s",
                      dcb,
                      STRDCBSTATE(dcb->state));
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
static void dcb_log_write_failure(DCB* dcb, GWBUF* queue, int eno)
{
    if (eno != EPIPE
        && eno != EAGAIN
        && eno != EWOULDBLOCK)
    {
        MXS_ERROR("Write to dcb %p in state %s fd %d failed: %d, %s",
                  dcb,
                  STRDCBSTATE(dcb->state),
                  dcb->fd,
                  eno,
                  mxs_strerror(eno));
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
int dcb_drain_writeq(DCB* dcb)
{
    if (dcb->ssl_read_want_write)
    {
        /** The SSL library needs to write more data */
        poll_fake_read_event(dcb);
    }

    int total_written = 0;
    GWBUF* local_writeq = dcb->writeq;
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

    if (dcb->writeq == NULL)
    {
        /* The write queue has drained, potentially need to call a callback function */
        dcb_call_callback(dcb, DCB_REASON_DRAINED);
    }

    mxb_assert(dcb->writeqlen >= (uint32_t)total_written);
    dcb->writeqlen -= total_written;

    if (dcb->high_water_reached && DCB_BELOW_LOW_WATER(dcb))
    {
        dcb_call_callback(dcb, DCB_REASON_LOW_WATER);
        dcb->high_water_reached = false;
        dcb->stats.n_low_water++;
    }

    return total_written;
}

static void log_illegal_dcb(DCB* dcb)
{
    const char* connected_to;

    switch (dcb->role)
    {
    case DCB::Role::BACKEND:
        connected_to = dcb->server->name();
        break;

    case DCB::Role::CLIENT:
        connected_to = dcb->remote;
        break;

    case DCB::Role::INTERNAL:
        connected_to = "Internal DCB";
        break;

    default:
        connected_to = "Illegal DCB role";
        break;
    }

    MXS_ERROR("Removing DCB %p but it is in state %s which is not legal for "
              "a call to dcb_close. The DCB is connected to: %s",
              dcb,
              STRDCBSTATE(dcb->state),
              connected_to);
}

/**
 * Closes a client/backend dcb, which in the former case always means that
 * the corrsponding socket fd is closed and the dcb itself is freed, and in
 * latter case either the same as in the former or that the dcb is put into
 * the persistent pool.
 *
 * @param dcb The DCB to close
 */
void dcb_close(DCB* dcb)
{
#if defined (SS_DEBUG)
    RoutingWorker* current = RoutingWorker::get_current();
    RoutingWorker* owner = static_cast<RoutingWorker*>(dcb->owner);
    if (current && (current != owner))
    {
        MXS_ALERT("dcb_close(%p) called by %d, owned by %d.",
                  dcb,
                  current->id(),
                  owner->id());
        mxb_assert(owner == RoutingWorker::get_current());
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

        RoutingWorker* worker = static_cast<RoutingWorker*>(dcb->owner);
        mxb_assert(worker);

        worker->register_zombie(dcb);
    }
    else
    {
        ++dcb->n_close;
        // TODO: Will this happen on a regular basis?
        MXS_WARNING("dcb_close(%p) called %u times.", dcb, dcb->n_close);
        mxb_assert(!true);
    }
}

static void cb_dcb_close_in_owning_thread(MXB_WORKER*, void* data)
{
    DCB* dcb = static_cast<DCB*>(data);
    mxb_assert(dcb);

    dcb_close(dcb);
}

void dcb_close_in_owning_thread(DCB* dcb)
{
    // TODO: If someone now calls dcb_close(dcb) from the owning thread while
    // TODO: the dcb is being delivered to the owning thread, there will be a
    // TODO: crash when dcb_close(dcb) is called anew. Also dcbs should be
    // TODO: reference counted, so that we could addref before posting, thus
    // TODO: preventing too early a deletion.

    MXB_WORKER* worker = static_cast<MXB_WORKER*>(dcb->owner);      // The owning worker
    mxb_assert(worker);

    intptr_t arg1 = (intptr_t)cb_dcb_close_in_owning_thread;
    intptr_t arg2 = (intptr_t)dcb;

    if (!mxb_worker_post_message(worker, MXB_WORKER_MSG_CALL, arg1, arg2))
    {
        MXS_ERROR("Could not post dcb for closing to the owning thread..");
    }
}

void dcb_final_close(DCB* dcb)
{
#if defined (SS_DEBUG)
    RoutingWorker* current = RoutingWorker::get_current();
    RoutingWorker* owner = static_cast<RoutingWorker*>(dcb->owner);
    if (current && (current != owner))
    {
        MXS_ALERT("dcb_final_close(%p) called by %d, owned by %d.",
                  dcb,
                  current->id(),
                  owner->id());
        mxb_assert(owner == current);
    }
#endif
    mxb_assert(dcb->n_close != 0);

    if (dcb->role == DCB::Role::BACKEND         // Backend DCB
        && dcb->state == DCB_STATE_POLLING      // Being polled
        && dcb->persistentstart == 0            /** Not already in (> 0) or being evicted from (-1)
                                                 * the persistent pool. */
        && dcb->server)                         // And has a server
    {
        /* May be a candidate for persistence, so save user name */
        const char* user;
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

        if (dcb->server)
        {
            // This is now a DCB::Role::BACKEND_HANDLER.
            // TODO: Make decisions according to the role and assert
            // TODO: that what the role implies is preset.
            mxb::atomic::add(&dcb->server->stats.n_current, -1, mxb::atomic::RELAXED);
        }

        if (dcb->fd != DCBFD_CLOSED)
        {
            // TODO: How could we get this far with a dcb->fd <= 0?

            if (close(dcb->fd) < 0)
            {
                int eno = errno;
                errno = 0;
                MXS_ERROR("Failed to close socket %d on dcb %p: %d, %s",
                          dcb->fd,
                          dcb,
                          eno,
                          mxs_strerror(eno));
            }
            else
            {
                dcb->fd = DCBFD_CLOSED;

                MXS_DEBUG("Closed socket %d on dcb %p.", dcb->fd, dcb);
            }
        }
        else
        {
            // Only internal DCBs are closed with a fd of -1
            mxb_assert(dcb->role == DCB::Role::INTERNAL);
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
static bool dcb_maybe_add_persistent(DCB* dcb)
{
    RoutingWorker* owner = static_cast<RoutingWorker*>(dcb->owner);
    Server* server = static_cast<Server*>(dcb->server);
    if (dcb->user != NULL
        && (dcb->func.established == NULL || dcb->func.established(dcb))
        && strlen(dcb->user)
        && server
        && dcb->session
        && session_valid_for_pool(dcb->session)
        && server->persistpoolmax()
        && (server->status & SERVER_RUNNING)
        && !dcb->dcb_errhandle_called
        && dcb_persistent_clean_count(dcb, owner->id(), false) < server->persistpoolmax()
        && mxb::atomic::load(&server->stats.n_persistent) < server->persistpoolmax())
    {
        DCB_CALLBACK* loopcallback;
        MXS_DEBUG("Adding DCB to persistent pool, user %s.", dcb->user);
        dcb->was_persistent = false;
        dcb->persistentstart = time(NULL);
        session_unlink_backend_dcb(dcb->session, dcb);
        dcb->session = nullptr;

        while ((loopcallback = dcb->callbacks) != NULL)
        {
            dcb->callbacks = loopcallback->next;
            MXS_FREE(loopcallback);
        }

        /** Free all buffered data */
        gwbuf_free(dcb->fakeq);
        gwbuf_free(dcb->readq);
        gwbuf_free(dcb->delayq);
        gwbuf_free(dcb->writeq);
        dcb->fakeq = NULL;
        dcb->readq = NULL;
        dcb->delayq = NULL;
        dcb->writeq = NULL;

        dcb->nextpersistent = server->persistent[owner->id()];
        server->persistent[owner->id()] = dcb;
        mxb::atomic::add(&dcb->server->stats.n_persistent, 1);
        mxb::atomic::add(&dcb->server->stats.n_current, -1, mxb::atomic::RELAXED);
        return true;
    }

    return false;
}

/**
 * Diagnostic to print a DCB
 *
 * @param dcb   The DCB to print
 *
 */
void printDCB(DCB* dcb)
{
    printf("DCB: %p\n", (void*)dcb);
    printf("\tDCB state:            %s\n", gw_dcb_state2string(dcb->state));
    if (dcb->remote)
    {
        printf("\tConnected to:         %s\n", dcb->remote);
    }
    if (dcb->user)
    {
        printf("\tUsername:             %s\n", dcb->user);
    }
    if (dcb->session->listener)
    {
        printf("\tProtocol:             %s\n", dcb->session->listener->protocol());
    }
    if (dcb->writeq)
    {
        printf("\tQueued write data:    %u\n", gwbuf_length(dcb->writeq));
    }
    if (dcb->server)
    {
        string statusname = dcb->server->status_string();
        if (!statusname.empty())
        {
            printf("\tServer status:            %s\n", statusname.c_str());
        }
    }
    char* rolename = dcb_role_name(dcb);
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
static void spin_reporter(void* dcb, char* desc, int value)
{
    dcb_printf((DCB*)dcb, "\t\t%-40s  %d\n", desc, value);
}

bool printAllDCBs_cb(DCB* dcb, void* data)
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
void dprintOneDCB(DCB* pdcb, DCB* dcb)
{
    dcb_printf(pdcb, "DCB: %p\n", (void*)dcb);
    dcb_printf(pdcb,
               "\tDCB state:          %s\n",
               gw_dcb_state2string(dcb->state));
    if (dcb->session && dcb->session->service)
    {
        dcb_printf(pdcb,
                   "\tService:            %s\n",
                   dcb->session->service->name());
    }
    if (dcb->remote)
    {
        dcb_printf(pdcb,
                   "\tConnected to:       %s\n",
                   dcb->remote);
    }
    if (dcb->server)
    {
        if (dcb->server->address)
        {
            dcb_printf(pdcb,
                       "\tServer name/IP:     %s\n",
                       dcb->server->address);
        }
        if (dcb->server->port)
        {
            dcb_printf(pdcb,
                       "\tPort number:        %d\n",
                       dcb->server->port);
        }
    }
    if (dcb->user)
    {
        dcb_printf(pdcb,
                   "\tUsername:           %s\n",
                   dcb->user);
    }
    if (dcb->session->listener)
    {
        dcb_printf(pdcb, "\tProtocol:           %s\n", dcb->session->listener->protocol());
    }
    if (dcb->writeq)
    {
        dcb_printf(pdcb,
                   "\tQueued write data:  %d\n",
                   gwbuf_length(dcb->writeq));
    }
    if (dcb->server)
    {
        string statusname = dcb->server->status_string();
        if (!statusname.empty())
        {
            dcb_printf(pdcb, "\tServer status:            %s\n", statusname.c_str());
        }
    }
    char* rolename = dcb_role_name(dcb);
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

static bool dprint_all_dcbs_cb(DCB* dcb, void* data)
{
    DCB* pdcb = (DCB*)data;
    dprintOneDCB(pdcb, dcb);
    return true;
}

/**
 * Diagnostic to print all DCB allocated in the system
 *
 * @param       pdcb    DCB to print results to
 */
void dprintAllDCBs(DCB* pdcb)
{
    dcb_foreach(dprint_all_dcbs_cb, pdcb);
}

static bool dlist_dcbs_cb(DCB* dcb, void* data)
{
    DCB* pdcb = (DCB*)data;
    dcb_printf(pdcb,
               " %-16p | %-26s | %-18s | %s\n",
               dcb,
               gw_dcb_state2string(dcb->state),
               ((dcb->session && dcb->session->service) ? dcb->session->service->name() : ""),
               (dcb->remote ? dcb->remote : ""));
    return true;
}

/**
 * Diagnostic routine to print DCB data in a tabular form.
 *
 * @param       pdcb    DCB to print results to
 */
void dListDCBs(DCB* pdcb)
{
    dcb_printf(pdcb, "Descriptor Control Blocks\n");
    dcb_printf(pdcb, "------------------+----------------------------+--------------------+----------\n");
    dcb_printf(pdcb,
               " %-16s | %-26s | %-18s | %s\n",
               "DCB",
               "State",
               "Service",
               "Remote");
    dcb_printf(pdcb, "------------------+----------------------------+--------------------+----------\n");
    dcb_foreach(dlist_dcbs_cb, pdcb);
    dcb_printf(pdcb, "------------------+----------------------------+--------------------+----------\n\n");
}

static bool dlist_clients_cb(DCB* dcb, void* data)
{
    DCB* pdcb = (DCB*)data;

    if (dcb->role == DCB::Role::CLIENT)
    {
        dcb_printf(pdcb,
                   " %-15s | %16p | %-20s | %10p\n",
                   (dcb->remote ? dcb->remote : ""),
                   dcb,
                   (dcb->session->service ?
                    dcb->session->service->name() : ""),
                   dcb->session);
    }

    return true;
}

/**
 * Diagnostic routine to print client DCB data in a tabular form.
 *
 * @param       pdcb    DCB to print results to
 */
void dListClients(DCB* pdcb)
{
    dcb_printf(pdcb, "Client Connections\n");
    dcb_printf(pdcb, "-----------------+------------------+----------------------+------------\n");
    dcb_printf(pdcb,
               " %-15s | %-16s | %-20s | %s\n",
               "Client",
               "DCB",
               "Service",
               "Session");
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
void dprintDCB(DCB* pdcb, DCB* dcb)
{
    dcb_printf(pdcb, "DCB: %p\n", (void*)dcb);
    dcb_printf(pdcb, "\tDCB state:          %s\n", gw_dcb_state2string(dcb->state));
    if (dcb->session && dcb->session->service)
    {
        dcb_printf(pdcb,
                   "\tService:            %s\n",
                   dcb->session->service->name());
    }
    if (dcb->remote)
    {
        dcb_printf(pdcb, "\tConnected to:               %s\n", dcb->remote);
    }
    if (dcb->user)
    {
        dcb_printf(pdcb,
                   "\tUsername:                   %s\n",
                   dcb->user);
    }
    if (dcb->session->listener)
    {
        dcb_printf(pdcb, "\tProtocol:                   %s\n", dcb->session->listener->protocol());
    }

    if (dcb->session)
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
    if (dcb->server)
    {
        string statusname = dcb->server->status_string();
        if (!statusname.c_str())
        {
            dcb_printf(pdcb, "\tServer status:            %s\n", statusname.c_str());
        }
    }
    char* rolename = dcb_role_name(dcb);
    if (rolename)
    {
        dcb_printf(pdcb, "\tRole:                     %s\n", rolename);
        MXS_FREE(rolename);
    }
    dcb_printf(pdcb, "\tStatistics:\n");
    dcb_printf(pdcb,
               "\t\tNo. of Reads:                     %d\n",
               dcb->stats.n_reads);
    dcb_printf(pdcb,
               "\t\tNo. of Writes:                    %d\n",
               dcb->stats.n_writes);
    dcb_printf(pdcb,
               "\t\tNo. of Buffered Writes:           %d\n",
               dcb->stats.n_buffered);
    dcb_printf(pdcb,
               "\t\tNo. of Accepts:                   %d\n",
               dcb->stats.n_accepts);
    dcb_printf(pdcb,
               "\t\tNo. of High Water Events: %d\n",
               dcb->stats.n_high_water);
    dcb_printf(pdcb,
               "\t\tNo. of Low Water Events:  %d\n",
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
const char* gw_dcb_state2string(dcb_state_t state)
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
void dcb_printf(DCB* dcb, const char* fmt, ...)
{
    GWBUF* buf;
    va_list args;

    if ((buf = gwbuf_alloc(10240)) == NULL)
    {
        return;
    }
    va_start(args, fmt);
    vsnprintf((char*)GWBUF_DATA(buf), 10240, fmt, args);
    va_end(args);

    buf->end = (void*)((char*)GWBUF_DATA(buf) + strlen((char*)GWBUF_DATA(buf)));
    dcb->func.write(dcb, buf);
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
static int gw_write_SSL(DCB* dcb, GWBUF* writeq, bool* stop_writing)
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
static int gw_write(DCB* dcb, GWBUF* writeq, bool* stop_writing)
{
    int written = 0;
    int fd = dcb->fd;
    size_t nbytes = GWBUF_LENGTH(writeq);
    void* buf = GWBUF_DATA(writeq);
    int saved_errno;

    errno = 0;

    if (fd != DCBFD_CLOSED)
    {
        written = write(fd, buf, nbytes);
    }

    saved_errno = errno;
    errno = 0;

    if (written < 0)
    {
        *stop_writing = true;
#if defined (SS_DEBUG)
        if (saved_errno != EAGAIN
            && saved_errno != EWOULDBLOCK)
#else
        if (saved_errno != EAGAIN
            && saved_errno != EWOULDBLOCK
            && saved_errno != EPIPE)
#endif
        {
            MXS_ERROR("Write to %s %s in state %s failed: %d, %s",
                      dcb->type(),
                      dcb->remote,
                      STRDCBSTATE(dcb->state),
                      saved_errno,
                      mxs_strerror(saved_errno));
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
int dcb_add_callback(DCB* dcb,
                     DCB_REASON reason,
                     int (* callback)(DCB*, DCB_REASON, void*),
                     void* userdata)
{
    DCB_CALLBACK* cb, * ptr, * lastcb = NULL;

    if ((ptr = (DCB_CALLBACK*)MXS_MALLOC(sizeof(DCB_CALLBACK))) == NULL)
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
        if (cb->reason == reason && cb->cb == callback
            && cb->userdata == userdata)
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
int dcb_remove_callback(DCB* dcb,
                        DCB_REASON reason,
                        int (* callback)(DCB*, DCB_REASON, void*),
                        void* userdata)
{
    DCB_CALLBACK* cb, * pcb = NULL;
    int rval = 0;
    cb = dcb->callbacks;

    if (cb == NULL)
    {
        rval = 0;
    }
    else
    {
        while (cb)
        {
            if (cb->reason == reason
                && cb->cb == callback
                && cb->userdata == userdata)
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
static void dcb_call_callback(DCB* dcb, DCB_REASON reason)
{
    DCB_CALLBACK* cb, * nextcb;
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

static void dcb_hangup_foreach_worker(MXB_WORKER* worker, struct SERVER* server)
{
    RoutingWorker* rworker = static_cast<RoutingWorker*>(worker);
    int id = rworker->id();

    for (DCB* dcb = this_unit.all_dcbs[id]; dcb; dcb = dcb->thread.next)
    {
        if (dcb->state == DCB_STATE_POLLING && dcb->server && dcb->server == server)
        {
            if (!dcb->dcb_errhandle_called)
            {
                dcb->func.hangup(dcb);
                dcb->dcb_errhandle_called = true;
            }
        }
    }
}

/**
 * Call all the callbacks on all DCB's that match the server and the reason given
 *
 * @param reason        The DCB_REASON that triggers the callback
 */
void dcb_hangup_foreach(struct SERVER* server)
{
    intptr_t arg1 = (intptr_t)dcb_hangup_foreach_worker;
    intptr_t arg2 = (intptr_t)server;

    RoutingWorker::broadcast_message(MXB_WORKER_MSG_CALL, arg1, arg2);
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
int dcb_persistent_clean_count(DCB* dcb, int id, bool cleanall)
{
    int count = 0;
    if (dcb && dcb->server)
    {
        Server* server = static_cast<Server*>(dcb->server);
        DCB* previousdcb = NULL;
        DCB* persistentdcb, * nextdcb;
        DCB* disposals = NULL;

        persistentdcb = server->persistent[id];
        while (persistentdcb)
        {
            nextdcb = persistentdcb->nextpersistent;
            if (cleanall
                || persistentdcb->dcb_errhandle_called
                || count >= server->persistpoolmax()
                || persistentdcb->server == NULL
                || !(persistentdcb->server->status & SERVER_RUNNING)
                || (time(NULL) - persistentdcb->persistentstart) > server->persistmaxtime())
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
                mxb::atomic::add(&server->stats.n_persistent, -1);
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
    int       count;
    DCB_USAGE type;
};

bool count_by_usage_cb(DCB* dcb, void* data)
{
    struct dcb_usage_count* d = (struct dcb_usage_count*)data;

    switch (d->type)
    {
    case DCB_USAGE_CLIENT:
        if (DCB::Role::CLIENT == dcb->role)
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
        if (dcb->role == DCB::Role::BACKEND)
        {
            d->count++;
        }
        break;

    case DCB_USAGE_INTERNAL:
        if (dcb->role == DCB::Role::CLIENT
            || dcb->role == DCB::Role::BACKEND)
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
int dcb_count_by_usage(DCB_USAGE usage)
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
static int dcb_create_SSL(DCB* dcb, SSL_LISTENER* ssl)
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
    if (NULL == dcb->session->listener->ssl()
        || (NULL == dcb->ssl && dcb_create_SSL(dcb, dcb->session->listener->ssl()) != 0))
    {
        return -1;
    }

    MXB_AT_DEBUG(const char* remote = dcb->remote ? dcb->remote : "");
    MXB_AT_DEBUG(const char* user = dcb->user ? dcb->user : "");

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

    if ((NULL == dcb->server || NULL == dcb->server->server_ssl)
        || (NULL == dcb->ssl && dcb_create_SSL(dcb, dcb->server->server_ssl) != 0))
    {
        mxb_assert((NULL != dcb->server) && (NULL != dcb->server->server_ssl));
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
static int dcb_set_socket_option(int sockfd, int level, int optname, void* optval, socklen_t optlen)
{
    if (setsockopt(sockfd, level, optname, optval, optlen) != 0)
    {
        MXS_ERROR("Failed to set socket options: %d, %s",
                  errno,
                  mxs_strerror(errno));
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
char* dcb_role_name(DCB* dcb)
{
    char* name = (char*)MXS_MALLOC(64);

    if (name)
    {
        name[0] = 0;
        if (DCB::Role::CLIENT == dcb->role)
        {
            strcat(name, "Client Request Handler");
        }
        else if (DCB::Role::BACKEND == dcb->role)
        {
            strcat(name, "Backend Request Handler");
        }
        else if (DCB::Role::INTERNAL == dcb->role)
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

static void dcb_add_to_list_cb(int thread_id, void* data)
{
    DCB* dcb = (DCB*)data;

    mxb_assert(thread_id == static_cast<RoutingWorker*>(dcb->owner)->id());

    dcb_add_to_list(dcb);
}

static void dcb_add_to_list(DCB* dcb)
{
    if (dcb->thread.next == NULL && dcb->thread.tail == NULL)
    {
        /**
         * This is a DCB which is either not a listener or it is a listener which
         * is not in the list. Stopped listeners are not removed from the list.
         */

        int id = static_cast<RoutingWorker*>(dcb->owner)->id();
        mxb_assert(id == RoutingWorker::get_current_id());

        if (this_unit.all_dcbs[id] == NULL)
        {
            this_unit.all_dcbs[id] = dcb;
            this_unit.all_dcbs[id]->thread.tail = dcb;
        }
        else
        {
            mxb_assert(this_unit.all_dcbs[id]->thread.tail->thread.next != dcb);
            this_unit.all_dcbs[id]->thread.tail->thread.next = dcb;
            this_unit.all_dcbs[id]->thread.tail = dcb;
        }
    }
}

/**
 * Remove a DCB from the owner's list
 *
 * @param dcb DCB to remove
 */
static void dcb_remove_from_list(DCB* dcb)
{
    int id = static_cast<RoutingWorker*>(dcb->owner)->id();

    if (dcb == this_unit.all_dcbs[id])
    {
        DCB* tail = this_unit.all_dcbs[id]->thread.tail;
        this_unit.all_dcbs[id] = this_unit.all_dcbs[id]->thread.next;

        if (this_unit.all_dcbs[id])
        {
            this_unit.all_dcbs[id]->thread.tail = tail;
        }
    }
    else
    {
        // If the creation of the DCB failed, it will not have been added
        // to the list at all. And if it happened to be the first DCB to be
        // created, then `prev` is NULL at this point.
        DCB* prev = this_unit.all_dcbs[id];
        DCB* current = prev ? prev->thread.next : NULL;

        while (current)
        {
            if (current == dcb)
            {
                if (current == this_unit.all_dcbs[id]->thread.tail)
                {
                    this_unit.all_dcbs[id]->thread.tail = prev;
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
    if (this_unit.check_timeouts && mxs_clock() >= this_thread.next_timeout_check)
    {
        /** Because the resolution of the timeout is one second, we only need to
         * check for it once per second. One heartbeat is 100 milliseconds. */
        this_thread.next_timeout_check = mxs_clock() + 10;

        for (DCB* dcb = this_unit.all_dcbs[thr]; dcb; dcb = dcb->thread.next)
        {
            if (dcb->role == DCB::Role::CLIENT)
            {
                SERVICE* service = dcb->session->service;

                if (service->conn_idle_timeout && dcb->state == DCB_STATE_POLLING)
                {
                    int64_t idle = mxs_clock() - dcb->last_read;
                    int64_t timeout = service->conn_idle_timeout * 10;

                    if (idle > timeout)
                    {
                        MXS_WARNING("Timing out '%s'@%s, idle for %.1f seconds",
                                    dcb->user ? dcb->user : "<unknown>",
                                    dcb->remote ? dcb->remote : "<unknown>",
                                    (float)idle / 10.f);
                        dcb->session->close_reason = SESSION_CLOSE_TIMEOUT;
                        poll_fake_hangup_event(dcb);
                    }
                }
            }
        }
    }
}

/** Helper class for serial iteration over all DCBs */
class SerialDcbTask : public Worker::Task
{
public:

    SerialDcbTask(bool (*func)(DCB*, void*), void* data)
        : m_func(func)
        , m_data(data)
        , m_more(1)
    {
    }

    void execute(Worker& worker)
    {
        RoutingWorker& rworker = static_cast<RoutingWorker&>(worker);
        int thread_id = rworker.id();

        for (DCB* dcb = this_unit.all_dcbs[thread_id];
             dcb && atomic_load_int32(&m_more);
             dcb = dcb->thread.next)
        {
            if (dcb->session)
            {
                if (!m_func(dcb, m_data))
                {
                    atomic_store_int32(&m_more, 0);
                    break;
                }
            }
            else
            {
                mxb_assert_message(dcb->persistentstart > 0, "The DCB must be in a connection pool");
            }
        }
    }

    bool more() const
    {
        return m_more;
    }

private:
    bool (* m_func)(DCB* dcb, void* data);
    void* m_data;
    int   m_more;
};

bool dcb_foreach(bool (* func)(DCB* dcb, void* data), void* data)
{
    mxb_assert(RoutingWorker::get_current() == RoutingWorker::get(RoutingWorker::MAIN));
    SerialDcbTask task(func, data);
    RoutingWorker::execute_serially(task);
    return task.more();
}

void dcb_foreach_local(bool (* func)(DCB* dcb, void* data), void* data)
{
    int thread_id = RoutingWorker::get_current_id();

    for (DCB* dcb = this_unit.all_dcbs[thread_id]; dcb; dcb = dcb->thread.next)
    {
        if (dcb->session)
        {
            mxb_assert(dcb->thread.next != dcb);

            if (!func(dcb, data))
            {
                break;
            }
        }
        else
        {
            mxb_assert_message(dcb->persistentstart > 0, "The DCB must be in a connection pool");
        }
    }
}

int dcb_get_port(const DCB* dcb)
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
        mxb_assert(dcb->ip.ss_family == AF_UNIX);
    }

    return rval;
}

static uint32_t dcb_process_poll_events(DCB* dcb, uint32_t events)
{
    RoutingWorker* owner = static_cast<RoutingWorker*>(dcb->owner);
    mxb_assert(owner == RoutingWorker::get_current());

    uint32_t rc = MXB_POLL_NOP;

    /*
     * It isn't obvious that this is impossible
     * mxb_assert(dcb->state != DCB_STATE_DISCONNECTED);
     */
    if (DCB_STATE_DISCONNECTED == dcb->state)
    {
        return rc;
    }

    if (dcb->n_close != 0)
    {
        MXS_WARNING("Events reported for dcb(%p), owned by %d, that has been closed %" PRIu32 " times.",
                    dcb,
                    owner->id(),
                    dcb->n_close);
        mxb_assert(!true);
        return rc;
    }

    /**
     * Any of these callbacks might close the DCB. Hence, the value of 'n_close'
     * must be checked after each callback invocation.
     */

    if ((events & EPOLLOUT) && (dcb->n_close == 0))
    {
        int eno = 0;
        eno = gw_getsockerrno(dcb->fd);

        if (eno == 0)
        {
            rc |= MXB_POLL_WRITE;

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
        MXS_DEBUG("%lu [poll_waitevents] "
                  "Read in dcb %p fd %d",
                  pthread_self(),
                  dcb,
                  dcb->fd);
        rc |= MXB_POLL_READ;

        if (dcb_session_check(dcb, "read"))
        {
            int return_code = 1;
            /** SSL authentication is still going on, we need to call dcb_accept_SSL
             * until it return 1 for success or -1 for error */
            if (dcb->ssl_state == SSL_HANDSHAKE_REQUIRED)
            {
                return_code = (DCB::Role::CLIENT == dcb->role) ?
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
    if ((events & EPOLLERR) && (dcb->n_close == 0))
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
        rc |= MXB_POLL_ERROR;

        if (dcb_session_check(dcb, "error"))
        {
            DCB_EH_NOTICE("Calling dcb->func.error(%p)", dcb);
            dcb->func.error(dcb);
        }
    }

    if ((events & EPOLLHUP) && (dcb->n_close == 0))
    {
        MXB_AT_DEBUG(int eno = gw_getsockerrno(dcb->fd));
        MXB_AT_DEBUG(char errbuf[MXS_STRERROR_BUFLEN]);
        MXS_DEBUG("%lu [poll_waitevents] "
                  "EPOLLHUP on dcb %p, fd %d. "
                  "Errno %d, %s.",
                  pthread_self(),
                  dcb,
                  dcb->fd,
                  eno,
                  strerror_r(eno, errbuf, sizeof(errbuf)));
        rc |= MXB_POLL_HUP;

        if (!dcb->dcb_errhandle_called)
        {
            if (dcb_session_check(dcb, "hangup EPOLLHUP"))
            {
                DCB_EH_NOTICE("Calling dcb->func.hangup(%p)", dcb);
                dcb->func.hangup(dcb);
            }

            dcb->dcb_errhandle_called = true;
        }
    }

#ifdef EPOLLRDHUP
    if ((events & EPOLLRDHUP) && (dcb->n_close == 0))
    {
        MXB_AT_DEBUG(int eno = gw_getsockerrno(dcb->fd));
        MXB_AT_DEBUG(char errbuf[MXS_STRERROR_BUFLEN]);
        MXS_DEBUG("%lu [poll_waitevents] "
                  "EPOLLRDHUP on dcb %p, fd %d. "
                  "Errno %d, %s.",
                  pthread_self(),
                  dcb,
                  dcb->fd,
                  eno,
                  strerror_r(eno, errbuf, sizeof(errbuf)));
        rc |= MXB_POLL_HUP;

        if (!dcb->dcb_errhandle_called)
        {
            if (dcb_session_check(dcb, "hangup EPOLLRDHUP"))
            {
                DCB_EH_NOTICE("Calling dcb->func.hangup(%p)", dcb);
                dcb->func.hangup(dcb);
            }

            dcb->dcb_errhandle_called = true;
        }
    }
#endif

    return rc;
}

static uint32_t dcb_handler(DCB* dcb, uint32_t events)
{
    this_thread.current_dcb = dcb;
    uint32_t rv = dcb_process_poll_events(dcb, events);

    // When all I/O events have been handled, we will immediately
    // process an added fake event. As the handling of a fake event
    // may lead to the addition of another fake event we loop until
    // there is no fake event or the dcb has been closed.

    while ((dcb->n_close == 0) && (dcb->fake_event != 0))
    {
        events = dcb->fake_event;
        dcb->fake_event = 0;

        rv |= dcb_process_poll_events(dcb, events);
    }

    this_thread.current_dcb = NULL;

    return rv;
}

static uint32_t dcb_poll_handler(MXB_POLL_DATA* data, MXB_WORKER* worker, uint32_t events)
{
    uint32_t rval = 0;
    DCB* dcb = (DCB*)data;

    /**
     * Fake hangup events (e.g. from monitors) can cause a DCB to be closed
     * before the real events are processed. This makes it look like a closed
     * DCB is receiving events when in reality the events were received at the
     * same time the DCB was closed. If a closed DCB receives events they should
     * be ignored.
     *
     * @see FakeEventTask()
     */
    if (dcb->n_close == 0)
    {
        rval = dcb_handler(dcb, events);
    }

    return rval;
}

static bool dcb_is_still_valid(DCB* target, int id)
{
    bool rval = false;

    for (DCB* dcb = this_unit.all_dcbs[id];
         dcb; dcb = dcb->thread.next)
    {
        if (target == dcb)
        {
            if (dcb->n_close == 0)
            {
                rval = true;
            }
            break;
        }
    }

    return rval;
}

class FakeEventTask : public Worker::DisposableTask
{
    FakeEventTask(const FakeEventTask&);
    FakeEventTask& operator=(const FakeEventTask&);

public:
    FakeEventTask(DCB* dcb, GWBUF* buf, uint32_t ev)
        : m_dcb(dcb)
        , m_buffer(buf)
        , m_ev(ev)
    {
    }

    void execute(Worker& worker)
    {
        mxb_assert(&worker == RoutingWorker::get_current());

        RoutingWorker& rworker = static_cast<RoutingWorker&>(worker);
        if (dcb_is_still_valid(m_dcb, rworker.id()))
        {
            m_dcb->fakeq = m_buffer;
            dcb_handler(m_dcb, m_ev);
        }
        else
        {
            gwbuf_free(m_buffer);
        }
    }

private:
    DCB*     m_dcb;
    GWBUF*   m_buffer;
    uint32_t m_ev;
};

static void poll_add_event_to_dcb(DCB* dcb, GWBUF* buf, uint32_t ev)
{
    if (dcb == this_thread.current_dcb)
    {
        // If the fake event is added to the current DCB, we arrange for
        // it to be handled immediately in dcb_handler() when the handling
        // of the current events are done...

        if (dcb->fake_event != 0)
        {
            MXS_WARNING("Events have already been injected to current DCB, discarding existing.");
            gwbuf_free(dcb->fakeq);
            dcb->fake_event = 0;
        }

        dcb->fakeq = buf;
        dcb->fake_event = ev;
    }
    else
    {
        // ... otherwise we post the fake event using the messaging mechanism.

        FakeEventTask* task = new(std::nothrow) FakeEventTask(dcb, buf, ev);

        if (task)
        {
            RoutingWorker* worker = static_cast<RoutingWorker*>(dcb->owner);
            worker->execute(std::unique_ptr<FakeEventTask>(task), Worker::EXECUTE_QUEUED);
        }
        else
        {
            MXS_OOM();
        }
    }
}

void poll_add_epollin_event_to_dcb(DCB* dcb, GWBUF* buf)
{
    poll_add_event_to_dcb(dcb, buf, EPOLLIN);
}

void poll_fake_write_event(DCB* dcb)
{
    poll_add_event_to_dcb(dcb, NULL, EPOLLOUT);
}

void poll_fake_read_event(DCB* dcb)
{
    poll_add_event_to_dcb(dcb, NULL, EPOLLIN);
}

void poll_fake_hangup_event(DCB* dcb)
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
static bool dcb_session_check(DCB* dcb, const char* function)
{
    if (dcb->session || dcb->persistentstart)
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

/** DCB Sanity checks */
static inline void dcb_sanity_check(DCB* dcb)
{
    if (dcb->state == DCB_STATE_DISCONNECTED || dcb->state == DCB_STATE_UNDEFINED)
    {
        MXS_ERROR("[poll_add_dcb] Error : existing state of dcb %p "
                  "is %s, but this should be impossible, crashing.",
                  dcb,
                  STRDCBSTATE(dcb->state));
        raise(SIGABRT);
    }
    else if (dcb->state == DCB_STATE_POLLING || dcb->state == DCB_STATE_LISTENING)
    {
        MXS_ERROR("[poll_add_dcb] Error : existing state of dcb %p "
                  "is %s, but this is probably an error, not crashing.",
                  dcb,
                  STRDCBSTATE(dcb->state));
    }
}

namespace
{

class AddDcbToWorker : public Worker::DisposableTask
{
public:
    AddDcbToWorker(const AddDcbToWorker&) = delete;
    AddDcbToWorker& operator=(const AddDcbToWorker&) = delete;

    AddDcbToWorker(DCB* dcb, uint32_t events)
        : m_dcb(dcb)
        , m_events(events)
    {
    }

    void execute(Worker& worker)
    {
        RoutingWorker& rworker = static_cast<RoutingWorker&>(worker);

        mxb_assert(rworker.id() == static_cast<RoutingWorker*>(m_dcb->owner)->id());

        bool added = dcb_add_to_worker(&rworker, m_dcb, m_events);
        mxb_assert(added);

        if (!added)
        {
            dcb_close(m_dcb);
        }
    }

private:
    DCB*     m_dcb;
    uint32_t m_events;
};
}

static bool add_fd_to_routing_workers(int fd, uint32_t events, MXB_POLL_DATA* data)
{
    bool rv = true;
    MXB_WORKER* previous_owner = data->owner;

    rv = RoutingWorker::add_shared_fd(fd, events, data);

    if (rv)
    {
        // The DCB will appear on the list of the calling thread.
        RoutingWorker* worker = RoutingWorker::get_current();

        if (!worker)
        {
            // TODO: Listeners are created before the workers have been started.
            // TODO: Hence there will be no current worker. So, we just store them
            // TODO: in the main worker.
            worker = RoutingWorker::get(RoutingWorker::MAIN);
        }

        data->owner = worker;
    }
    else
    {
        // Restore the situation.
        data->owner = previous_owner;
    }

    return rv;
}

static bool dcb_add_to_worker(Worker* worker, DCB* dcb, uint32_t events)
{
    mxb_assert(worker == dcb->owner);
    bool rv = false;

    if (worker == RoutingWorker::get_current())
    {
        // If the DCB should end up on the current thread, we can both add it
        // to the epoll-instance and to the DCB book-keeping immediately.
        if (worker->add_fd(dcb->fd, events, (MXB_POLL_DATA*)dcb))
        {
            dcb_add_to_list(dcb);
            rv = true;
        }
    }
    else
    {
        // Otherwise we'll move the whole operation to the correct worker.
        // This will only happen for "cli" and "maxinfo" services that must
        // be served by one thread as there otherwise deadlocks can occur.
        AddDcbToWorker* task = new(std::nothrow) AddDcbToWorker(dcb, events);
        mxb_assert(task);

        if (task)
        {
            Worker* worker = static_cast<RoutingWorker*>(dcb->owner);
            mxb_assert(worker);

            if (worker->execute(std::unique_ptr<AddDcbToWorker>(task), Worker::EXECUTE_QUEUED))
            {
                rv = true;
            }
            else
            {
                MXS_ERROR("Could not post task to add DCB to worker.");
            }
        }
        else
        {
            MXS_OOM();
        }
    }

    return rv;
}

int poll_add_dcb(DCB* dcb)
{
    dcb_sanity_check(dcb);

    uint32_t events = poll_events;

    /** Choose new state and worker thread ID according to the role of DCB. */
    dcb_state_t new_state;
    RoutingWorker* owner = nullptr;

    if (dcb->role == DCB::Role::CLIENT)
    {
        if (strcasecmp(dcb->service->router_name(), "cli") == 0
            || strcasecmp(dcb->service->router_name(), "maxinfo") == 0)
        {
            // If the DCB refers to an accepted maxadmin/maxinfo socket, we force it
            // to the main thread. That's done in order to prevent a deadlock
            // that may happen if there are multiple concurrent administrative calls,
            // handled by different worker threads.
            // See: https://jira.mariadb.org/browse/MXS-1805 and https://jira.mariadb.org/browse/MXS-1833
            owner = RoutingWorker::get(RoutingWorker::MAIN);
        }
        else if (dcb->state == DCB_STATE_NOPOLLING)
        {
            // This DCB was removed and added back to epoll. Assign it to the same worker it started with.
            owner = static_cast<RoutingWorker*>(dcb->owner);
        }
        else
        {
            // Assign to current worker
            owner = RoutingWorker::get_current();
        }

        new_state = DCB_STATE_POLLING;
        dcb->owner = owner;
    }
    else
    {
        mxb_assert(dcb->role == DCB::Role::BACKEND);
        mxb_assert(RoutingWorker::get_current_id() != -1);
        mxb_assert(RoutingWorker::get_current() == dcb->owner);

        new_state = DCB_STATE_POLLING;
        owner = static_cast<RoutingWorker*>(dcb->owner);
    }

    /**
     * Assign the new state before adding the DCB to the worker and store the
     * old state in case we need to revert it.
     */
    dcb_state_t old_state = dcb->state;
    dcb->state = new_state;

    int rc = 0;

    if (!dcb_add_to_worker(owner, dcb, events))
    {
        /**
         * We failed to add the DCB to a worker. Revert the state so that it
         * will be treated as a DCB in the correct state. As this will involve
         * cleanup, ensure that the current thread is the owner, as otherwise
         * debug asserts will be triggered.
         */
        dcb->state = old_state;
        dcb->owner = RoutingWorker::get_current();
        rc = -1;
    }

    return rc;
}

int poll_remove_dcb(DCB* dcb)
{
    int dcbfd, rc = 0;
    struct  epoll_event ev;

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
    mxb_assert(dcbfd != DCBFD_CLOSED || dcb->role == DCB::Role::INTERNAL);

    if (dcbfd != DCBFD_CLOSED)
    {
        rc = -1;

        Worker* worker = static_cast<Worker*>(dcb->owner);
        mxb_assert(worker);

        if (worker->remove_fd(dcbfd))
        {
            rc = 0;
        }
    }
    return rc;
}

DCB* dcb_get_current()
{
    return this_thread.current_dcb;
}

/**
 * @brief DCB callback for upstream throtting
 * Called by any backend dcb when its writeq is above high water mark or
 * it has reached high water mark and now it is below low water mark,
 * Calling `poll_remove_dcb` or `poll_add_dcb' on client dcb to throttle
 * network traffic from client to mxs.
 *
 * @param dcb      Backend dcb
 * @param reason   Why the callback was called
 * @param userdata Data provided when the callback was added
 * @return Always 0
 */
static int upstream_throttle_callback(DCB* dcb, DCB_REASON reason, void* userdata)
{
    DCB* client_dcb = dcb->session->client_dcb;
    mxb::Worker* worker = static_cast<mxb::Worker*>(client_dcb->owner);

    // The fd is removed manually here due to the fact that poll_add_dcb causes the DCB to be added to the
    // worker's list of DCBs but poll_remove_dcb doesn't remove it from it. This is due to the fact that the
    // DCBs are only removed from the list when they are closed.
    if (reason == DCB_REASON_HIGH_WATER)
    {
        MXS_INFO("High water mark hit for '%s'@'%s', not reading data until low water mark is hit",
                 client_dcb->user, client_dcb->remote);
        worker->remove_fd(client_dcb->fd);
        client_dcb->state = DCB_STATE_NOPOLLING;
    }
    else if (reason == DCB_REASON_LOW_WATER)
    {
        MXS_INFO("Low water mark hit for '%s'@'%s', accepting new data", client_dcb->user, client_dcb->remote);
        worker->add_fd(client_dcb->fd, poll_events, (MXB_POLL_DATA*)client_dcb);
        client_dcb->state = DCB_STATE_POLLING;
    }

    return 0;
}

bool backend_dcb_remove_func(DCB* dcb, void* data)
{
    MXS_SESSION* session = (MXS_SESSION*)data;

    if (dcb->session == session && dcb->role == DCB::Role::BACKEND)
    {
        DCB* client_dcb = dcb->session->client_dcb;
        MXS_INFO("High water mark hit for connection to '%s' from %s'@'%s', not reading data until low water "
                 "mark is hit", dcb->server->name(), client_dcb->user, client_dcb->remote);

        mxb::Worker* worker = static_cast<mxb::Worker*>(dcb->owner);
        worker->remove_fd(dcb->fd);
        dcb->state = DCB_STATE_NOPOLLING;
    }

    return true;
}

bool backend_dcb_add_func(DCB* dcb, void* data)
{
    MXS_SESSION* session = (MXS_SESSION*)data;

    if (dcb->session == session && dcb->role == DCB::Role::BACKEND)
    {
        DCB* client_dcb = dcb->session->client_dcb;
        MXS_INFO("Low water mark hit for  connection to '%s' from '%s'@'%s', accepting new data",
                 dcb->server->name(), client_dcb->user, client_dcb->remote);

        mxb::Worker* worker = static_cast<mxb::Worker*>(dcb->owner);
        worker->add_fd(dcb->fd, poll_events, (MXB_POLL_DATA*)dcb);
        dcb->state = DCB_STATE_POLLING;
    }

    return true;
}

/**
 * @brief DCB callback for downstream throtting
 * Called by client dcb when its writeq is above high water mark or
 * it has reached high water mark and now it is below low water mark,
 * Calling `poll_remove_dcb` or `poll_add_dcb' on all backend dcbs to
 * throttle network traffic from server to mxs.
 *
 * @param dcb      client dcb
 * @param reason   Why the callback was called
 * @param userdata Data provided when the callback was added
 * @return Always 0
 */
static int downstream_throttle_callback(DCB* dcb, DCB_REASON reason, void* userdata)
{
    if (reason == DCB_REASON_HIGH_WATER)
    {
        dcb_foreach_local(backend_dcb_remove_func, dcb->session);
    }
    else if (reason == DCB_REASON_LOW_WATER)
    {
        dcb_foreach_local(backend_dcb_add_func, dcb->session);
    }

    return 0;
}

json_t* dcb_to_json(DCB* dcb)
{
    json_t* obj = json_object();

    char buf[25];
    snprintf(buf, sizeof(buf), "%p", dcb);
    json_object_set_new(obj, "id", json_string(buf));
    json_object_set_new(obj, "server", json_string(dcb->server->name()));

    if (dcb->func.diagnostics_json)
    {
        json_t* json = dcb->func.diagnostics_json(dcb);
        mxb_assert(json);
        json_object_set_new(obj, "protocol_diagnostics", json);
    }

    return obj;
}

const char* DCB::type()
{
    switch (role)
    {
    case DCB::Role::CLIENT:
        return "Client DCB";

    case DCB::Role::BACKEND:
        return "Backend DCB";

    case DCB::Role::INTERNAL:
        return "Internal DCB";

    default:
        mxb_assert(!true);
        return "Unknown DCB";
    }
}
