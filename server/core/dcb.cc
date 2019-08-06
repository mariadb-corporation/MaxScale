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

#include <atomic>

#include <maxbase/alloc.h>
#include <maxbase/atomic.h>
#include <maxbase/atomic.hh>
#include <maxscale/authenticator2.hh>
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
using mxs::AuthenticatorBackendSession;

// #define DCB_LOG_EVENT_HANDLING
#if defined (DCB_LOG_EVENT_HANDLING)
#define DCB_EH_NOTICE(s, p) MXS_NOTICE(s, p)
#else
#define DCB_EH_NOTICE(s, p)
#endif

#define DCB_BELOW_LOW_WATER(x)    ((x)->m_low_water && (x)->m_writeqlen < (x)->m_low_water)
#define DCB_ABOVE_HIGH_WATER(x)   ((x)->m_high_water && (x)->m_writeqlen > (x)->m_high_water)
#define DCB_THROTTLING_ENABLED(x) ((x)->m_high_water && (x)->m_low_water)

namespace
{

static struct THIS_UNIT
{
    bool                  check_timeouts;   /**< Should session timeouts be checked. */
    std::atomic<uint64_t> uid_generator {0};
#ifdef EPOLLRDHUP
    static constexpr uint32_t poll_events = EPOLLIN | EPOLLOUT | EPOLLRDHUP | EPOLLHUP | EPOLLET;
#else
    static constexpr uint32_t poll_events = EPOLLIN | EPOLLOUT | EPOLLHUP | EPOLLET;
#endif
} this_unit;

static thread_local struct
{
    long next_timeout_check;/** When to next check for idle sessions. */
    DCB* current_dcb;       /** The DCB currently being handled by event handlers. */
} this_thread;
}

static void        dcb_initialize(DCB* dcb);
static void        dcb_call_callback(DCB* dcb, DCB_REASON reason);
static int         dcb_null_write(DCB* dcb, GWBUF* buf);
static int         dcb_null_auth(DCB* dcb, SERVER* server, MXS_SESSION* session, GWBUF* buf);
static inline DCB* dcb_find_in_list(DCB* dcb);
static void        dcb_stop_polling_and_shutdown(DCB* dcb);
inline bool        dcb_maybe_add_persistent(DCB* dcb)
{
    return DCB::maybe_add_persistent(dcb);
}
static inline bool dcb_write_parameter_check(DCB* dcb, GWBUF* queue);
static int         dcb_read_no_bytes_available(DCB* dcb, int nreadtotal);
static GWBUF*      dcb_basic_read(DCB* dcb,
                                  int bytesavailable,
                                  int maxbytes,
                                  int nreadtotal,
                                  int* nsingleread);
static void   dcb_log_write_failure(DCB* dcb, GWBUF* queue, int eno);
static int    gw_write(DCB* dcb, GWBUF* writeq, bool* stop_writing);
static int    dcb_log_errors_SSL(DCB* dcb, int ret);
static int    dcb_set_socket_option(int sockfd, int level, int optname, void* optval, socklen_t optlen);
static void   dcb_add_to_all_list(DCB* dcb);
static DCB*   dcb_find_free();

static uint32_t dcb_poll_handler(MXB_POLL_DATA* data, MXB_WORKER* worker, uint32_t events);
static uint32_t dcb_process_poll_events(DCB* dcb, uint32_t ev);
static bool     dcb_session_check(DCB* dcb, const char*);
static int      upstream_throttle_callback(DCB* dcb, DCB_REASON reason, void* userdata);
static int      downstream_throttle_callback(DCB* dcb, DCB_REASON reason, void* userdata);

void dcb_global_init()
{
}

void dcb_finish()
{
    // TODO: Free all resources.
}

uint64_t dcb_get_session_id(DCB* dcb)
{
    return (dcb && dcb->session()) ? dcb->session()->id() : 0;
}

static MXB_WORKER* get_dcb_owner()
{
    /** The DCB is owned by the thread that allocates it */
    mxb_assert(RoutingWorker::get_current_id() != -1);
    return RoutingWorker::get_current();
}

DCB::DCB(Role role, MXS_SESSION* session, SERVER* server, Registry* registry)
    : MXB_POLL_DATA{dcb_poll_handler, get_dcb_owner()}
    , m_high_water(config_writeq_high_water())
    , m_low_water(config_writeq_low_water())
    , m_last_read(mxs_clock())
    , m_last_write(mxs_clock())
    , m_server(server)
    , m_uid(this_unit.uid_generator.fetch_add(1, std::memory_order_relaxed))
    , m_session(session)
    , m_role(role)
    , m_registry(registry)
{
    // TODO: Remove DCB::Role::INTERNAL to always have a valid listener
    if (session->listener)
    {
        m_func = session->listener->protocol_func();
    }

    if (DCB* client = session->client_dcb)
    {
        if (client->m_remote)
        {
            m_remote = MXS_STRDUP_A(client->m_remote);
        }
        if (client->m_user)
        {
            m_user = MXS_STRDUP_A(client->m_user);
        }
    }

    if (m_high_water && m_low_water && m_role == DCB::Role::CLIENT)
    {
        dcb_add_callback(this, DCB_REASON_HIGH_WATER, downstream_throttle_callback, NULL);
        dcb_add_callback(this, DCB_REASON_LOW_WATER, downstream_throttle_callback, NULL);
    }

    if (m_registry)
    {
        m_registry->add(this);
    }
}

DCB::~DCB()
{
    if (m_registry)
    {
        m_registry->remove(this);
    }

    if (m_data)
    {
        m_authenticator_data->free_data(this);
    }

    delete m_authenticator_data;

    while (m_callbacks)
    {
        DCB_CALLBACK* tmp = m_callbacks;
        m_callbacks = m_callbacks->next;
        MXS_FREE(tmp);
    }

    if (m_ssl)
    {
        SSL_free(m_ssl);
    }

    MXS_FREE(m_remote);
    MXS_FREE(m_user);
    gwbuf_free(m_delayq);
    gwbuf_free(m_writeq);
    gwbuf_free(m_readq);
    gwbuf_free(m_fakeq);

    MXB_POLL_DATA::owner = reinterpret_cast<MXB_WORKER*>(0xdeadbeef);
}

ClientDCB* dcb_create_client(MXS_SESSION* session, DCB::Registry* registry)
{
    return new (std::nothrow) ClientDCB(session, registry);
}

InternalDCB* dcb_create_internal(MXS_SESSION* session, DCB::Registry* registry)
{
    return new (std::nothrow) InternalDCB(session, registry);
}

/**
 * Free a DCB and remove it from the chain of all DCBs
 *
 * @param dcb The DCB to free
 */
//static
void DCB::final_free(DCB* dcb)
{
    mxb_assert_message(dcb->state() == DCB_STATE_DISCONNECTED || dcb->state() == DCB_STATE_ALLOC,
                       "dcb not in DCB_STATE_DISCONNECTED not in DCB_STATE_ALLOC state.");

    if (dcb->m_session)
    {
        /*<
         * Terminate client session.
         */
        MXS_SESSION* local_session = dcb->m_session;
        dcb->m_session = NULL;
        if (dcb->m_role == DCB::Role::BACKEND)
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

            mxb_assert(dcb->m_role == DCB::Role::CLIENT || dcb->m_role == DCB::Role::INTERNAL);
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
    dcb->disable_events();
    /**
     * close protocol and router session
     */
    if (dcb->m_func.close != NULL)
    {
        dcb->shutdown();
        dcb->m_func.close(dcb);
        dcb->m_protocol = nullptr;
    }
}

static DCB* take_from_connection_pool(Server* server, MXS_SESSION* session, const char* protocol)
{
    DCB* dcb = nullptr;

    if (server->persistent_conns_enabled())
    {
        // TODO: figure out if this can even be NULL
        if (const char* user = session_get_user(session))
        {
            auto owner = static_cast<RoutingWorker*>(session->client_dcb->owner);
            auto dcb = server->get_persistent_dcb(user, session->client_dcb->m_remote, protocol, owner->id());

            if (dcb)
            {
                MXS_DEBUG("Reusing a persistent connection, user %s, dcb %p", user, dcb);
                session_link_backend_dcb(session, dcb);
                dcb->m_persistentstart = 0;
                dcb->m_was_persistent = true;
                dcb->m_last_read = mxs_clock();
                dcb->m_last_write = mxs_clock();
                mxb::atomic::add(&server->pool_stats.n_from_pool, 1, mxb::atomic::RELAXED);
                return dcb;
            }
        }
    }

    return nullptr;
}

BackendDCB* dcb_alloc_backend_dcb(Server* server, MXS_SESSION* session, const char* protocol, DCB::Registry* registry)
{
    MXS_PROTOCOL* funcs = (MXS_PROTOCOL*)load_module(protocol, MODULE_PROTOCOL);

    if (!funcs)
    {
        return nullptr;
    }

    mxb_assert_message(funcs->auth_default, "Module '%s' does not define auth_default method", protocol);
    string authenticator = server->get_authenticator();

    if (authenticator.empty())
    {
        authenticator = funcs->auth_default();
    }

    // Allocate DCB specific authentication data. Backend authenticators do not have an instance.
    AuthenticatorBackendSession* auth_session = nullptr;
    // If possible, use the client session to generate the backend authenticator.
    MXS_AUTHENTICATOR* authfuncs = nullptr;
    if (session->listener->auth_instance()->capabilities() & mxs::Authenticator::CAP_BACKEND_AUTH)
    {
        auth_session = session->client_dcb->m_authenticator_data->newBackendSession();
    }
    else
    {
        authfuncs = (MXS_AUTHENTICATOR*)load_module(authenticator.c_str(), MODULE_AUTHENTICATOR);
        if (authfuncs && authfuncs->create)
        {
            auth_session = static_cast<AuthenticatorBackendSession*>(authfuncs->create(nullptr));
        }
    }

    if (!auth_session)
    {
        MXS_ERROR("Failed to create authenticator session for backend DCB.");
        return nullptr;
    }

    BackendDCB* dcb = new (std::nothrow) BackendDCB(session, server, registry);

    if (dcb)
    {
        memcpy(&dcb->m_func, funcs, sizeof(dcb->m_func));
        dcb->m_authenticator_data = auth_session;

        session_link_backend_dcb(session, dcb);
    }
    else
    {
        delete auth_session;
    }

    return dcb;
}

/**
 * Connect to a server
 *
 * @param host The host to connect to
 * @param port The port to connect to
 * @param fd   FD is stored in this pointer
 *
 * @return True on success, false on error
 */
static bool do_connect(const char* host, int port, int* fd)
{
    bool ok = false;
    struct sockaddr_storage addr = {};
    int so;
    size_t sz;

    if (host[0] == '/')
    {
        so = open_unix_socket(MXS_SOCKET_NETWORK, (struct sockaddr_un*)&addr, host);
        sz = sizeof(sockaddr_un);
    }
    else
    {
        so = open_network_socket(MXS_SOCKET_NETWORK, &addr, host, port);
        sz = sizeof(sockaddr_storage);
    }

    if (so != -1)
    {
        if (connect(so, (struct sockaddr*)&addr, sz) == -1 && errno != EINPROGRESS)
        {
            MXS_ERROR("Failed to connect backend server [%s]:%d due to: %d, %s.",
                      host, port, errno, mxs_strerror(errno));
            close(so);
        }
        else
        {
            *fd = so;
            ok = true;
        }
    }
    else
    {
        MXS_ERROR("Establishing connection to backend server [%s]:%d failed.", host, port);
    }

    return ok;
}

/**
 * Connect to a backend server
 *
 * @param srv      The server to connect to
 * @param session  The session this connection is being made for
 * @param registry The DCB registry to use
 *
 * @return The new allocated dcb or NULL on error
 */
BackendDCB* BackendDCB::connect(SERVER* srv, MXS_SESSION* session, DCB::Registry* registry)
{
    const char* protocol = srv->protocol().c_str();
    Server* server = static_cast<Server*>(srv);

    // TODO: Either
    // - ignore that the provided registry may be different that the one used,
    // - remove the DCB from its registry when moved to the pool and assign a new one when it is
    //   taken out from the pool, or
    // - also consider the registry when deciding whether a DCB in the pool can be used or not.
    if (auto dcb = take_from_connection_pool(server, session, protocol))
    {
        // TODO: For now, we ignore the problem.
        return static_cast<BackendDCB*>(dcb);     // Reusing a DCB from the connection pool
    }

    // Could not find a reusable DCB, allocate a new one
    BackendDCB* dcb = dcb_alloc_backend_dcb(server, session, protocol, registry);

    if (dcb)
    {
        if (do_connect(server->address, server->port, &dcb->m_fd)
            && (dcb->m_protocol = dcb->m_func.connect(dcb, server, session))
            && dcb->enable_events())
        {
            // The DCB is now connected and added to epoll set. Authentication is done after the EPOLLOUT
            // event that is triggered once the connection is established.

            if (DCB_THROTTLING_ENABLED(dcb))
            {
                // Register upstream throttling callbacks
                dcb_add_callback(dcb, DCB_REASON_HIGH_WATER, upstream_throttle_callback, NULL);
                dcb_add_callback(dcb, DCB_REASON_LOW_WATER, upstream_throttle_callback, NULL);
            }

            mxb::atomic::add(&server->stats().n_connections, 1, mxb::atomic::RELAXED);
            mxb::atomic::add(&server->stats().n_current, 1, mxb::atomic::RELAXED);
        }
        else
        {
            if (dcb->m_fd != DCBFD_CLOSED)
            {
                ::close(dcb->m_fd);
            }
            session_unlink_backend_dcb(dcb->session(), dcb);
            dcb_free_all_memory(dcb);
            dcb = nullptr;
        }
    }

    return dcb;
}

int DCB::read(GWBUF** head, int maxbytes)
{
    mxb_assert(this->owner == RoutingWorker::get_current());
    int nsingleread = 0;
    int nreadtotal = 0;

    if (m_readq)
    {
        *head = gwbuf_append(*head, m_readq);
        m_readq = NULL;
        nreadtotal = gwbuf_length(*head);
    }
    else if (m_fakeq)
    {
        *head = gwbuf_append(*head, m_fakeq);
        m_fakeq = NULL;
        nreadtotal = gwbuf_length(*head);
    }

    if (SSL_HANDSHAKE_DONE == m_ssl_state || SSL_ESTABLISHED == m_ssl_state)
    {
        return read_SSL(head);
    }

    if (m_fd == DCBFD_CLOSED)
    {
        MXS_ERROR("Read failed, dcb is closed.");
        return 0;
    }

    while (0 == maxbytes || nreadtotal < maxbytes)
    {
        int bytes_available;

        bytes_available = dcb_bytes_readable(this);
        if (bytes_available <= 0)
        {
            return bytes_available < 0 ? -1
                                       :/** Handle closed client socket */
                   dcb_read_no_bytes_available(this, nreadtotal);
        }
        else
        {
            GWBUF* buffer;
            m_last_read = mxs_clock();

            buffer = dcb_basic_read(this, bytes_available, maxbytes, nreadtotal, &nsingleread);
            if (buffer)
            {
                nreadtotal += nsingleread;
                MXS_DEBUG("Read %d bytes from dcb %p in state %s fd %d.",
                          nsingleread,
                          this,
                          STRDCBSTATE(m_state),
                          m_fd);

                /*< Assign the target server for the gwbuf */
                buffer->server = m_server;
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
 *
 * @return          -1 on error, otherwise the total number of bytes available
 */
int dcb_bytes_readable(DCB* dcb)
{
    int bytesavailable;

    if (-1 == ioctl(dcb->m_fd, FIONREAD, &bytesavailable))
    {
        MXS_ERROR("ioctl FIONREAD for dcb %p in state %s fd %d failed: %d, %s",
                  dcb,
                  STRDCBSTATE(dcb->state()),
                  dcb->m_fd,
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
    if (nreadtotal == 0 && DCB::Role::CLIENT == dcb->role())
    {
        char c;
        int l_errno = 0;
        long r = -1;

        /* try to read 1 byte, without consuming the socket buffer */
        r = recv(dcb->m_fd, &c, sizeof(char), MSG_PEEK);
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
        *nsingleread = read(dcb->m_fd, GWBUF_DATA(buffer), bufsize);
        dcb->m_stats.n_reads++;

        if (*nsingleread <= 0)
        {
            if (errno != 0 && errno != EAGAIN && errno != EWOULDBLOCK)
            {
                MXS_ERROR("Read failed, dcb %p in state %s fd %d: %d, %s",
                          dcb,
                          STRDCBSTATE(dcb->state()),
                          dcb->m_fd,
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
int DCB::read_SSL(GWBUF** head)
{
    GWBUF* buffer;
    int nsingleread = 0, nreadtotal = 0;
    int start_length = *head ? gwbuf_length(*head) : 0;

    if (m_fd == DCBFD_CLOSED)
    {
        MXS_ERROR("Read failed, dcb is closed.");
        return -1;
    }

    if (m_ssl_write_want_read)
    {
        drain_writeq();
    }

    m_last_read = mxs_clock();
    buffer = basic_read_SSL(&nsingleread);
    if (buffer)
    {
        nreadtotal += nsingleread;
        *head = gwbuf_append(*head, buffer);

        while (buffer)
        {
            m_last_read = mxs_clock();
            buffer = basic_read_SSL(&nsingleread);
            if (buffer)
            {
                nreadtotal += nsingleread;
                /*< Append read data to the gwbuf */
                *head = gwbuf_append(*head, buffer);
            }
        }
    }

    mxb_assert((*head ? gwbuf_length(*head) : 0) == (size_t)(start_length + nreadtotal));

    return nsingleread < 0 ? nsingleread : nreadtotal;
}

/**
 * Basic read function to carry out a single read on the DCB's SSL connection
 *
 * @param dcb           The DCB to read from
 * @param nsingleread   To be set as the number of bytes read this time
 * @return              GWBUF* buffer containing the data, or null.
 */
GWBUF* DCB::basic_read_SSL(int* nsingleread)
{
    const size_t MXS_SO_RCVBUF_SIZE = (128 * 1024);
    unsigned char temp_buffer[MXS_SO_RCVBUF_SIZE];
    GWBUF* buffer = NULL;

    *nsingleread = SSL_read(m_ssl, temp_buffer, MXS_SO_RCVBUF_SIZE);

    m_stats.n_reads++;

    switch (SSL_get_error(m_ssl, *nsingleread))
    {
    case SSL_ERROR_NONE:
        /* Successful read */
        if (*nsingleread && (buffer = gwbuf_alloc_and_load(*nsingleread, (void*)temp_buffer)) == NULL)
        {
            *nsingleread = -1;
            return NULL;
        }
        /* If we were in a retry situation, need to clear flag and attempt write */
        if (m_ssl_read_want_write || m_ssl_read_want_read)
        {
            m_ssl_read_want_write = false;
            m_ssl_read_want_read = false;
            drain_writeq();
        }
        break;

    case SSL_ERROR_ZERO_RETURN:
        /* react to the SSL connection being closed */
        poll_fake_hangup_event(this);
        *nsingleread = 0;
        break;

    case SSL_ERROR_WANT_READ:
        /* Prevent SSL I/O on connection until retried, return to poll loop */
        m_ssl_read_want_write = false;
        m_ssl_read_want_read = true;
        *nsingleread = 0;
        break;

    case SSL_ERROR_WANT_WRITE:
        /* Prevent SSL I/O on connection until retried, return to poll loop */
        m_ssl_read_want_write = true;
        m_ssl_read_want_read = false;
        *nsingleread = 0;
        break;

    case SSL_ERROR_SYSCALL:
        *nsingleread = dcb_log_errors_SSL(this, *nsingleread);
        break;

    default:
        *nsingleread = dcb_log_errors_SSL(this, *nsingleread);
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
                  STRDCBSTATE(dcb->state()),
                  dcb->m_fd,
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

bool DCB::write(GWBUF* queue)
{
    mxb_assert(this->owner == RoutingWorker::get_current());
    m_writeqlen += gwbuf_length(queue);
    // The following guarantees that queue is not NULL
    if (!dcb_write_parameter_check(this, queue))
    {
        return 0;
    }

    m_writeq = gwbuf_append(m_writeq, queue);
    m_stats.n_buffered++;
    drain_writeq();

    if (DCB_ABOVE_HIGH_WATER(this) && !m_high_water_reached)
    {
        dcb_call_callback(this, DCB_REASON_HIGH_WATER);
        m_high_water_reached = true;
        m_stats.n_high_water++;
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

    if (dcb->m_fd == DCBFD_CLOSED)
    {
        MXS_ERROR("Write failed, dcb is closed.");
        gwbuf_free(queue);
        return false;
    }

    if (dcb->session() == NULL || dcb->session()->state() != MXS_SESSION::State::STOPPING)
    {
        /**
         * MXS_SESSION::State::STOPPING means that one of the backends is closing
         * the router session. Some backends may have not completed
         * authentication yet and thus they have no information about router
         * being closed. Session state is changed to MXS_SESSION::State::STOPPING
         * before router's closeSession is called and that tells that DCB may
         * still be writable.
         */
        if (dcb->state() != DCB_STATE_ALLOC
            && dcb->state() != DCB_STATE_POLLING
            && dcb->state() != DCB_STATE_NOPOLLING)
        {
            MXS_DEBUG("Write aborted to dcb %p because it is in state %s",
                      dcb,
                      STRDCBSTATE(dcb->state()));
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
                  STRDCBSTATE(dcb->state()),
                  dcb->m_fd,
                  eno,
                  mxs_strerror(eno));
    }
}

int DCB::drain_writeq()
{
    mxb_assert(this->owner == RoutingWorker::get_current());

    if (m_ssl_read_want_write)
    {
        /** The SSL library needs to write more data */
        poll_fake_read_event(this);
    }

    int total_written = 0;
    GWBUF* local_writeq = m_writeq;
    m_writeq = NULL;

    while (local_writeq)
    {
        int written;
        bool stop_writing = false;
        /* The value put into written will be >= 0 */
        if (m_ssl)
        {
            written = write_SSL(local_writeq, &stop_writing);
        }
        else
        {
            written = gw_write(this, local_writeq, &stop_writing);
        }
        /*
         * If the stop_writing boolean is set, writing has become blocked,
         * so the remaining data is put back at the front of the write
         * queue.
         */
        if (written)
        {
            m_last_write = mxs_clock();
        }
        if (stop_writing)
        {
            m_writeq = m_writeq ? gwbuf_append(local_writeq, m_writeq) : local_writeq;
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

    if (m_writeq == NULL)
    {
        /* The write queue has drained, potentially need to call a callback function */
        dcb_call_callback(this, DCB_REASON_DRAINED);
    }

    mxb_assert(m_writeqlen >= (uint32_t)total_written);
    m_writeqlen -= total_written;

    if (m_high_water_reached && DCB_BELOW_LOW_WATER(this))
    {
        dcb_call_callback(this, DCB_REASON_LOW_WATER);
        m_high_water_reached = false;
        m_stats.n_low_water++;
    }

    return total_written;
}

static void log_illegal_dcb(DCB* dcb)
{
    const char* connected_to;

    switch (dcb->role())
    {
    case DCB::Role::BACKEND:
        connected_to = dcb->m_server->name();
        break;

    case DCB::Role::CLIENT:
        connected_to = dcb->m_remote;
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
              STRDCBSTATE(dcb->state()),
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
//static
void DCB::close(DCB* dcb)
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

    if (DCB_STATE_DISCONNECTED == dcb->state())
    {
        log_illegal_dcb(dcb);
        raise(SIGABRT);
    }

    /**
     * dcb_close may be called for freshly created dcb, in which case
     * it only needs to be freed.
     */
    if (dcb->state() == DCB_STATE_ALLOC && dcb->m_fd == DCBFD_CLOSED)
    {
        // A freshly created dcb that was closed before it was taken into use.
        DCB::final_free(dcb);
    }
    /*
     * If DCB is in persistent pool, mark it as an error and exit
     */
    else if (dcb->m_persistentstart > 0)
    {
        // A DCB in the persistent pool.

        // TODO: This dcb will now actually be closed when dcb_persistent_clean_count() is
        // TODO: called by either dcb_maybe_add_persistent() - another dcb is added to the
        // TODO: persistent pool - or server_get_persistent() - get a dcb from the persistent
        // TODO: pool - is called. There is no reason not to just remove this dcb from the
        // TODO: persistent pool here and now, and then close it immediately.
        dcb->m_dcb_errhandle_called = true;
    }
    else if (dcb->m_nClose == 0)
    {
        dcb->m_nClose = 1;

        RoutingWorker* worker = static_cast<RoutingWorker*>(dcb->owner);
        mxb_assert(worker);

        worker->register_zombie(dcb);
    }
    else
    {
        ++dcb->m_nClose;
        // TODO: Will this happen on a regular basis?
        MXS_WARNING("dcb_close(%p) called %u times.", dcb, dcb->m_nClose);
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

//static
void DCB::final_close(DCB* dcb)
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
    mxb_assert(dcb->m_nClose != 0);

    if (dcb->role() == DCB::Role::BACKEND         // Backend DCB
        && dcb->state() == DCB_STATE_POLLING      // Being polled
        && dcb->m_persistentstart == 0            /** Not already in (> 0) or being evicted from (-1)
                                                 * the persistent pool. */
        && dcb->m_server)                         // And has a server
    {
        /* May be a candidate for persistence, so save user name */
        const char* user;
        user = session_get_user(dcb->m_session);
        if (user && strlen(user) && !dcb->m_user)
        {
            dcb->m_user = MXS_STRDUP_A(user);
        }

        if (dcb_maybe_add_persistent(dcb))
        {
            dcb->m_nClose = 0;
        }
    }

    if (dcb->m_nClose != 0)
    {
        if (dcb->state() == DCB_STATE_POLLING)
        {
            dcb_stop_polling_and_shutdown(dcb);
        }

        if (dcb->m_server && dcb->m_persistentstart == 0)
        {
            // This is now a DCB::Role::BACKEND_HANDLER.
            // TODO: Make decisions according to the role and assert
            // TODO: that what the role implies is preset.
            MXB_AT_DEBUG(int rc = ) mxb::atomic::add(&dcb->m_server->stats().n_current, -1,
                                                     mxb::atomic::RELAXED);
            mxb_assert(rc > 0);
        }

        if (dcb->m_fd != DCBFD_CLOSED)
        {
            // TODO: How could we get this far with a dcb->m_fd <= 0?

            if (::close(dcb->m_fd) < 0)
            {
                int eno = errno;
                errno = 0;
                MXS_ERROR("Failed to close socket %d on dcb %p: %d, %s",
                          dcb->m_fd,
                          dcb,
                          eno,
                          mxs_strerror(eno));
            }
            else
            {
                dcb->m_fd = DCBFD_CLOSED;

                MXS_DEBUG("Closed socket %d on dcb %p.", dcb->m_fd, dcb);
            }
        }
        else
        {
            // Only internal DCBs are closed with a fd of -1
            mxb_assert(dcb->role() == DCB::Role::INTERNAL);
        }

        dcb->m_state = DCB_STATE_DISCONNECTED;
        DCB::final_free(dcb);
    }
}

/**
 * Add DCB to persistent pool if it qualifies, close otherwise
 *
 * @param dcb   The DCB to go to persistent pool or be closed
 * @return      bool - whether the DCB was added to the pool
 *
 */
//static
bool DCB::maybe_add_persistent(DCB* dcb)
{
    RoutingWorker* owner = static_cast<RoutingWorker*>(dcb->owner);
    Server* server = static_cast<Server*>(dcb->m_server);
    if (dcb->m_user != NULL
        && (dcb->m_func.established == NULL || dcb->m_func.established(dcb))
        && strlen(dcb->m_user)
        && server
        && dcb->session()
        && session_valid_for_pool(dcb->session())
        && server->persistpoolmax()
        && server->is_running()
        && !dcb->m_dcb_errhandle_called
        && dcb_persistent_clean_count(dcb, owner->id(), false) < server->persistpoolmax())
    {
        if (!mxb::atomic::add_limited(&server->pool_stats.n_persistent, 1, (int)server->persistpoolmax()))
        {
            return false;
        }

        DCB_CALLBACK* loopcallback;
        MXS_DEBUG("Adding DCB to persistent pool, user %s.", dcb->m_user);
        dcb->m_was_persistent = false;
        dcb->m_persistentstart = time(NULL);
        session_unlink_backend_dcb(dcb->session(), dcb);
        dcb->m_session = nullptr;

        while ((loopcallback = dcb->m_callbacks) != NULL)
        {
            dcb->m_callbacks = loopcallback->next;
            MXS_FREE(loopcallback);
        }

        /** Free all buffered data */
        gwbuf_free(dcb->m_fakeq);
        gwbuf_free(dcb->m_readq);
        gwbuf_free(dcb->m_delayq);
        gwbuf_free(dcb->m_writeq);
        dcb->m_fakeq = NULL;
        dcb->m_readq = NULL;
        dcb->m_delayq = NULL;
        dcb->m_writeq = NULL;

        dcb->m_nextpersistent = server->persistent[owner->id()];
        server->persistent[owner->id()] = dcb;
        MXB_AT_DEBUG(int rc = ) mxb::atomic::add(&server->stats().n_current, -1, mxb::atomic::RELAXED);
        mxb_assert(rc > 0);
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
    printf("\tDCB state:            %s\n", gw_dcb_state2string(dcb->state()));
    if (dcb->m_remote)
    {
        printf("\tConnected to:         %s\n", dcb->m_remote);
    }
    if (dcb->m_user)
    {
        printf("\tUsername:             %s\n", dcb->m_user);
    }
    if (dcb->session()->listener)
    {
        printf("\tProtocol:             %s\n", dcb->session()->listener->protocol());
    }
    if (dcb->m_writeq)
    {
        printf("\tQueued write data:    %u\n", gwbuf_length(dcb->m_writeq));
    }
    if (dcb->m_server)
    {
        string statusname = dcb->m_server->status_string();
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
           dcb->m_stats.n_reads);
    printf("\t\tNo. of Writes:                      %d\n",
           dcb->m_stats.n_writes);
    printf("\t\tNo. of Buffered Writes:             %d\n",
           dcb->m_stats.n_buffered);
    printf("\t\tNo. of Accepts:                     %d\n",
           dcb->m_stats.n_accepts);
    printf("\t\tNo. of High Water Events:   %d\n",
           dcb->m_stats.n_high_water);
    printf("\t\tNo. of Low Water Events:    %d\n",
           dcb->m_stats.n_low_water);
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
               gw_dcb_state2string(dcb->state()));
    if (dcb->session() && dcb->session()->service)
    {
        dcb_printf(pdcb,
                   "\tService:            %s\n",
                   dcb->session()->service->name());
    }
    if (dcb->m_remote)
    {
        dcb_printf(pdcb,
                   "\tConnected to:       %s\n",
                   dcb->m_remote);
    }
    if (dcb->m_server)
    {
        if (dcb->m_server->address)
        {
            dcb_printf(pdcb,
                       "\tServer name/IP:     %s\n",
                       dcb->m_server->address);
        }
        if (dcb->m_server->port)
        {
            dcb_printf(pdcb,
                       "\tPort number:        %d\n",
                       dcb->m_server->port);
        }
    }
    if (dcb->m_user)
    {
        dcb_printf(pdcb,
                   "\tUsername:           %s\n",
                   dcb->m_user);
    }
    if (dcb->session()->listener)
    {
        dcb_printf(pdcb, "\tProtocol:           %s\n", dcb->session()->listener->protocol());
    }
    if (dcb->m_writeq)
    {
        dcb_printf(pdcb,
                   "\tQueued write data:  %d\n",
                   gwbuf_length(dcb->m_writeq));
    }
    if (dcb->m_server)
    {
        string statusname = dcb->m_server->status_string();
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
    dcb_printf(pdcb, "\t\tNo. of Reads:             %d\n", dcb->m_stats.n_reads);
    dcb_printf(pdcb, "\t\tNo. of Writes:            %d\n", dcb->m_stats.n_writes);
    dcb_printf(pdcb, "\t\tNo. of Buffered Writes:   %d\n", dcb->m_stats.n_buffered);
    dcb_printf(pdcb, "\t\tNo. of Accepts:           %d\n", dcb->m_stats.n_accepts);
    dcb_printf(pdcb, "\t\tNo. of High Water Events: %d\n", dcb->m_stats.n_high_water);
    dcb_printf(pdcb, "\t\tNo. of Low Water Events:  %d\n", dcb->m_stats.n_low_water);

    if (dcb->m_persistentstart)
    {
        char buff[20];
        struct tm timeinfo;
        localtime_r(&dcb->m_persistentstart, &timeinfo);
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
               gw_dcb_state2string(dcb->state()),
               ((dcb->session() && dcb->session()->service) ? dcb->session()->service->name() : ""),
               (dcb->m_remote ? dcb->m_remote : ""));
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

    if (dcb->role() == DCB::Role::CLIENT)
    {
        dcb_printf(pdcb,
                   " %-15s | %16p | %-20s | %10p\n",
                   (dcb->m_remote ? dcb->m_remote : ""),
                   dcb,
                   (dcb->session()->service ?
                    dcb->session()->service->name() : ""),
                   dcb->session());
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
    dcb_printf(pdcb, "\tDCB state:          %s\n", gw_dcb_state2string(dcb->state()));
    if (dcb->session() && dcb->session()->service)
    {
        dcb_printf(pdcb,
                   "\tService:            %s\n",
                   dcb->session()->service->name());
    }
    if (dcb->m_remote)
    {
        dcb_printf(pdcb, "\tConnected to:               %s\n", dcb->m_remote);
    }
    if (dcb->m_user)
    {
        dcb_printf(pdcb,
                   "\tUsername:                   %s\n",
                   dcb->m_user);
    }
    if (dcb->session()->listener)
    {
        dcb_printf(pdcb, "\tProtocol:                   %s\n", dcb->session()->listener->protocol());
    }

    if (dcb->session())
    {
        dcb_printf(pdcb, "\tOwning Session:     %" PRIu64 "\n", dcb->session()->id());
    }

    if (dcb->m_writeq)
    {
        dcb_printf(pdcb, "\tQueued write data:  %d\n", gwbuf_length(dcb->m_writeq));
    }
    if (dcb->m_delayq)
    {
        dcb_printf(pdcb, "\tDelayed write data: %d\n", gwbuf_length(dcb->m_delayq));
    }
    if (dcb->m_server)
    {
        string statusname = dcb->m_server->status_string();
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
    dcb_printf(pdcb,
               "\t\tNo. of Reads:                     %d\n",
               dcb->m_stats.n_reads);
    dcb_printf(pdcb,
               "\t\tNo. of Writes:                    %d\n",
               dcb->m_stats.n_writes);
    dcb_printf(pdcb,
               "\t\tNo. of Buffered Writes:           %d\n",
               dcb->m_stats.n_buffered);
    dcb_printf(pdcb,
               "\t\tNo. of Accepts:                   %d\n",
               dcb->m_stats.n_accepts);
    dcb_printf(pdcb,
               "\t\tNo. of High Water Events: %d\n",
               dcb->m_stats.n_high_water);
    dcb_printf(pdcb,
               "\t\tNo. of Low Water Events:  %d\n",
               dcb->m_stats.n_low_water);

    if (dcb->m_persistentstart)
    {
        char buff[20];
        struct tm timeinfo;
        localtime_r(&dcb->m_persistentstart, &timeinfo);
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

    case DCB_STATE_DISCONNECTED:
        return "DCB socket closed";

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
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(nullptr, 0, fmt, args);
    va_end(args);

    GWBUF* buf = gwbuf_alloc(n + 1);

    if (buf)
    {
        va_start(args, fmt);
        vsnprintf((char*)GWBUF_DATA(buf), n + 1, fmt, args);
        va_end(args);

        // Remove the trailing null character
        GWBUF_RTRIM(buf, 1);
        dcb->m_func.write(dcb, buf);
    }
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
int DCB::write_SSL(GWBUF* writeq, bool* stop_writing)
{
    int written;

    written = SSL_write(m_ssl, GWBUF_DATA(writeq), GWBUF_LENGTH(writeq));

    *stop_writing = false;
    switch ((SSL_get_error(m_ssl, written)))
    {
    case SSL_ERROR_NONE:
        /* Successful write */
        m_ssl_write_want_read = false;
        m_ssl_write_want_write = false;
        break;

    case SSL_ERROR_ZERO_RETURN:
        /* react to the SSL connection being closed */
        *stop_writing = true;
        poll_fake_hangup_event(this);
        break;

    case SSL_ERROR_WANT_READ:
        /* Prevent SSL I/O on connection until retried, return to poll loop */
        *stop_writing = true;
        m_ssl_write_want_read = true;
        m_ssl_write_want_write = false;
        break;

    case SSL_ERROR_WANT_WRITE:
        /* Prevent SSL I/O on connection until retried, return to poll loop */
        *stop_writing = true;
        m_ssl_write_want_read = false;
        m_ssl_write_want_write = true;
        break;

    case SSL_ERROR_SYSCALL:
        *stop_writing = true;
        if (dcb_log_errors_SSL(this, written) < 0)
        {
            poll_fake_hangup_event(this);
        }
        break;

    default:
        /* Report error(s) and shutdown the connection */
        *stop_writing = true;
        if (dcb_log_errors_SSL(this, written) < 0)
        {
            poll_fake_hangup_event(this);
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
    int fd = dcb->m_fd;
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
        if (saved_errno != EAGAIN && saved_errno != EWOULDBLOCK && saved_errno != EPIPE)
        {
            MXS_ERROR("Write to %s %s in state %s failed: %d, %s",
                      mxs::to_string(dcb->role()),
                      dcb->m_remote,
                      STRDCBSTATE(dcb->state()),
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
    cb = dcb->m_callbacks;

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
        dcb->m_callbacks = ptr;
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
    cb = dcb->m_callbacks;

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
                    dcb->m_callbacks = cb->next;
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
    cb = dcb->m_callbacks;

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
    DCB* old_current = this_thread.current_dcb;

    for (DCB* dcb : rworker->dcbs())
    {
        if (dcb->state() == DCB_STATE_POLLING && dcb->m_server && dcb->m_server == server && dcb->m_nClose == 0)
        {
            if (!dcb->m_dcb_errhandle_called)
            {
                this_thread.current_dcb = dcb;
                dcb->m_func.hangup(dcb);
                dcb->m_dcb_errhandle_called = true;
            }
        }
    }

    this_thread.current_dcb = old_current;
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
    if (dcb && dcb->m_server)
    {
        Server* server = static_cast<Server*>(dcb->m_server);
        DCB* previousdcb = NULL;
        DCB* persistentdcb, * nextdcb;
        DCB* disposals = NULL;

        persistentdcb = server->persistent[id];
        while (persistentdcb)
        {
            nextdcb = persistentdcb->m_nextpersistent;
            if (cleanall
                || persistentdcb->m_dcb_errhandle_called
                || count >= server->persistpoolmax()
                || persistentdcb->m_server == NULL
                || !(persistentdcb->m_server->status() & SERVER_RUNNING)
                || (time(NULL) - persistentdcb->m_persistentstart) > server->persistmaxtime())
            {
                /* Remove from persistent pool */
                if (previousdcb)
                {
                    previousdcb->m_nextpersistent = nextdcb;
                }
                else
                {
                    server->persistent[id] = nextdcb;
                }
                /* Add removed DCBs to disposal list for processing outside spinlock */
                persistentdcb->m_nextpersistent = disposals;
                disposals = persistentdcb;
                mxb::atomic::add(&server->pool_stats.n_persistent, -1);
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
            nextdcb = disposals->m_nextpersistent;
            disposals->m_persistentstart = -1;
            if (DCB_STATE_POLLING == disposals->state())
            {
                dcb_stop_polling_and_shutdown(disposals);
            }
            dcb_close(disposals);
            disposals = nextdcb;
        }
    }
    return count;
}

struct dcb_role_count
{
    int       count;
    DCB::Role role;
};

bool count_by_role_cb(DCB* dcb, void* data)
{
    struct dcb_role_count* d = (struct dcb_role_count*)data;

    if (dcb->role() == d->role)
    {
        d->count++;
    }

    return true;
}

int dcb_count_by_role(DCB::Role role)
{
    struct dcb_role_count val = {};
    val.count = 0;
    val.role = role;

    dcb_foreach(count_by_role_cb, &val);

    return val.count;
}

/**
 * Create the SSL structure for this DCB.
 * This function creates the SSL structure for the given SSL context.
 * This context should be the context of the service.
 * @param       dcb
 * @return      -1 on error, 0 otherwise.
 */
int DCB::create_SSL(mxs::SSLContext* ssl)
{
    m_ssl = ssl->open();

    if (!m_ssl)
    {
        MXS_ERROR("Failed to initialize SSL for connection.");
        return -1;
    }

    if (SSL_set_fd(m_ssl, m_fd) == 0)
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
int ClientDCB::ssl_handshake()
{
    if (!m_session->listener->ssl().context()
        || (!m_ssl && create_SSL(m_session->listener->ssl().context()) != 0))
    {
        return -1;
    }

    MXB_AT_DEBUG(const char* remote = m_remote ? m_remote : "");
    MXB_AT_DEBUG(const char* user = m_user ? m_user : "");

    int ssl_rval = SSL_accept(m_ssl);

    switch (SSL_get_error(m_ssl, ssl_rval))
    {
    case SSL_ERROR_NONE:
        MXS_DEBUG("SSL_accept done for %s@%s", user, remote);
        m_ssl_state = SSL_ESTABLISHED;
        m_ssl_read_want_write = false;
        return 1;

    case SSL_ERROR_WANT_READ:
        MXS_DEBUG("SSL_accept ongoing want read for %s@%s", user, remote);
        return 0;

    case SSL_ERROR_WANT_WRITE:
        MXS_DEBUG("SSL_accept ongoing want write for %s@%s", user, remote);
        m_ssl_read_want_write = true;
        return 0;

    case SSL_ERROR_ZERO_RETURN:
        MXS_DEBUG("SSL error, shut down cleanly during SSL accept %s@%s", user, remote);
        dcb_log_errors_SSL(this, 0);
        poll_fake_hangup_event(this);
        return 0;

    case SSL_ERROR_SYSCALL:
        MXS_DEBUG("SSL connection SSL_ERROR_SYSCALL error during accept %s@%s", user, remote);
        if (dcb_log_errors_SSL(this, ssl_rval) < 0)
        {
            m_ssl_state = SSL_HANDSHAKE_FAILED;
            poll_fake_hangup_event(this);
            return -1;
        }
        else
        {
            return 0;
        }

    default:
        MXS_DEBUG("SSL connection shut down with error during SSL accept %s@%s", user, remote);
        if (dcb_log_errors_SSL(this, ssl_rval) < 0)
        {
            m_ssl_state = SSL_HANDSHAKE_FAILED;
            poll_fake_hangup_event(this);
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
int BackendDCB::ssl_handshake()
{
    int ssl_rval;
    int return_code;

    if ((NULL == m_server || NULL == m_server->ssl().context())
        || (NULL == m_ssl && create_SSL(m_server->ssl().context()) != 0))
    {
        mxb_assert((NULL != m_server) && (NULL != m_server->ssl().context()));
        return -1;
    }
    m_ssl_state = SSL_HANDSHAKE_REQUIRED;
    ssl_rval = SSL_connect(m_ssl);
    switch (SSL_get_error(m_ssl, ssl_rval))
    {
    case SSL_ERROR_NONE:
        MXS_DEBUG("SSL_connect done for %s", m_remote);
        m_ssl_state = SSL_ESTABLISHED;
        m_ssl_read_want_write = false;
        return_code = 1;
        break;

    case SSL_ERROR_WANT_READ:
        MXS_DEBUG("SSL_connect ongoing want read for %s", m_remote);
        return_code = 0;
        break;

    case SSL_ERROR_WANT_WRITE:
        MXS_DEBUG("SSL_connect ongoing want write for %s", m_remote);
        m_ssl_read_want_write = true;
        return_code = 0;
        break;

    case SSL_ERROR_ZERO_RETURN:
        MXS_DEBUG("SSL error, shut down cleanly during SSL connect %s", m_remote);
        if (dcb_log_errors_SSL(this, 0) < 0)
        {
            poll_fake_hangup_event(this);
        }
        return_code = 0;
        break;

    case SSL_ERROR_SYSCALL:
        MXS_DEBUG("SSL connection shut down with SSL_ERROR_SYSCALL during SSL connect %s", m_remote);
        if (dcb_log_errors_SSL(this, ssl_rval) < 0)
        {
            m_ssl_state = SSL_HANDSHAKE_FAILED;
            poll_fake_hangup_event(this);
            return_code = -1;
        }
        else
        {
            return_code = 0;
        }
        break;

    default:
        MXS_DEBUG("SSL connection shut down with error during SSL connect %s", m_remote);
        if (dcb_log_errors_SSL(this, ssl_rval) < 0)
        {
            m_ssl_state = SSL_HANDSHAKE_FAILED;
            poll_fake_hangup_event(this);
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
        if (DCB::Role::CLIENT == dcb->role())
        {
            strcat(name, "Client Request Handler");
        }
        else if (DCB::Role::BACKEND == dcb->role())
        {
            strcat(name, "Backend Request Handler");
        }
        else if (DCB::Role::INTERNAL == dcb->role())
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
 * Enable the timing out of idle connections.
 */
void dcb_enable_session_timeouts()
{
    this_unit.check_timeouts = true;
}

/**
 * Close sessions that have been idle or write to the socket has taken for too long.
 *
 * If the time since a session last sent data is greater than the set connection_timeout
 * value in the service, it is disconnected. If the time since last write
 * to the socket is greater net_write_timeout the session is also disconnected.
 * The timeouts are disabled by default.
 */
void dcb_process_timeouts(int thr)
{
    if (this_unit.check_timeouts && mxs_clock() >= this_thread.next_timeout_check)
    {
        /** Because the resolutions of the timeouts is one second, we only need to
         * check them once per second. One heartbeat is 100 milliseconds. */
        this_thread.next_timeout_check = mxs_clock() + 10;

        RoutingWorker* rworker = RoutingWorker::get_current();

        for (DCB* dcb : rworker->dcbs())
        {
            if (dcb->role() == DCB::Role::CLIENT && dcb->state() == DCB_STATE_POLLING)
            {
                SERVICE* service = dcb->session()->service;

                if (service->conn_idle_timeout)
                {
                    int64_t idle = mxs_clock() - dcb->m_last_read;
                    // Multiply by 10 to match conn_idle_timeout resolution
                    // to the 100 millisecond tics.
                    int64_t timeout = service->conn_idle_timeout * 10;

                    if (idle > timeout)
                    {
                        MXS_WARNING("Timing out '%s'@%s, idle for %.1f seconds",
                                    dcb->m_user ? dcb->m_user : "<unknown>",
                                    dcb->m_remote ? dcb->m_remote : "<unknown>",
                                    (float)idle / 10.f);
                        dcb->session()->close_reason = SESSION_CLOSE_TIMEOUT;
                        poll_fake_hangup_event(dcb);
                    }
                }

                if (service->net_write_timeout && dcb->m_writeqlen > 0)
                {
                    int64_t idle = mxs_clock() - dcb->m_last_write;
                    // Multiply by 10 to match net_write_timeout resolution
                    // to the 100 millisecond tics.
                    if (idle > dcb->service()->net_write_timeout * 10)
                    {
                        MXS_WARNING("network write timed out for '%s'@%s, ",
                                    dcb->m_user ? dcb->m_user : "<unknown>",
                                    dcb->m_remote ? dcb->m_remote : "<unknown>");
                        dcb->session()->close_reason = SESSION_CLOSE_TIMEOUT;
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
        const auto& dcbs = rworker.dcbs();

        for (auto it = dcbs.begin();
             it != dcbs.end() && atomic_load_int32(&m_more);
             ++it)
        {
            DCB* dcb = *it;

            if (dcb->session())
            {
                if (!m_func(dcb, m_data))
                {
                    atomic_store_int32(&m_more, 0);
                    break;
                }
            }
            else
            {
                mxb_assert_message(dcb->m_persistentstart > 0, "The DCB must be in a connection pool");
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
    RoutingWorker* worker = RoutingWorker::get_current();
    const auto& dcbs = worker->dcbs();

    for (DCB* dcb : dcbs)
    {
        if (dcb->session())
        {
            if (!func(dcb, data))
            {
                break;
            }
        }
        else
        {
            mxb_assert_message(dcb->m_persistentstart > 0, "The DCB must be in a connection pool");
        }
    }
}

int dcb_get_port(const DCB* dcb)
{
    int rval = -1;

    if (dcb->m_ip.ss_family == AF_INET)
    {
        struct sockaddr_in* ip = (struct sockaddr_in*)&dcb->m_ip;
        rval = ntohs(ip->sin_port);
    }
    else if (dcb->m_ip.ss_family == AF_INET6)
    {
        struct sockaddr_in6* ip = (struct sockaddr_in6*)&dcb->m_ip;
        rval = ntohs(ip->sin6_port);
    }
    else
    {
        mxb_assert(dcb->m_ip.ss_family == AF_UNIX);
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
     * mxb_assert(dcb->state() != DCB_STATE_DISCONNECTED);
     */
    if (DCB_STATE_DISCONNECTED == dcb->state())
    {
        return rc;
    }

    if (dcb->m_nClose != 0)
    {
        MXS_WARNING("Events reported for dcb(%p), owned by %d, that has been closed %" PRIu32 " times.",
                    dcb,
                    owner->id(),
                    dcb->m_nClose);
        mxb_assert(!true);
        return rc;
    }

    /**
     * Any of these callbacks might close the DCB. Hence, the value of 'n_close'
     * must be checked after each callback invocation.
     */

    if ((events & EPOLLOUT) && (dcb->m_nClose == 0))
    {
        int eno = 0;
        eno = gw_getsockerrno(dcb->m_fd);

        if (eno == 0)
        {
            rc |= MXB_POLL_WRITE;

            if (dcb_session_check(dcb, "write_ready"))
            {
                DCB_EH_NOTICE("Calling dcb->m_func.write_ready(%p)", dcb);
                dcb->m_func.write_ready(dcb);
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
                      dcb->m_fd);
        }
    }
    if ((events & EPOLLIN) && (dcb->m_nClose == 0))
    {
        MXS_DEBUG("%lu [poll_waitevents] "
                  "Read in dcb %p fd %d",
                  pthread_self(),
                  dcb,
                  dcb->m_fd);
        rc |= MXB_POLL_READ;

        if (dcb_session_check(dcb, "read"))
        {
            int return_code = 1;
            /** SSL authentication is still going on, we need to call DCB::ssl_handehake
             * until it return 1 for success or -1 for error */
            if (dcb->m_ssl_state == SSL_HANDSHAKE_REQUIRED)
            {
                return_code = dcb->ssl_handshake();
            }
            if (1 == return_code)
            {
                DCB_EH_NOTICE("Calling dcb->m_func.read(%p)", dcb);
                dcb->m_func.read(dcb);
            }
        }
    }
    if ((events & EPOLLERR) && (dcb->m_nClose == 0))
    {
        int eno = gw_getsockerrno(dcb->m_fd);
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
            DCB_EH_NOTICE("Calling dcb->m_func.error(%p)", dcb);
            dcb->m_func.error(dcb);
        }
    }

    if ((events & EPOLLHUP) && (dcb->m_nClose == 0))
    {
        MXB_AT_DEBUG(int eno = gw_getsockerrno(dcb->m_fd));
        MXB_AT_DEBUG(char errbuf[MXS_STRERROR_BUFLEN]);
        MXS_DEBUG("%lu [poll_waitevents] "
                  "EPOLLHUP on dcb %p, fd %d. "
                  "Errno %d, %s.",
                  pthread_self(),
                  dcb,
                  dcb->m_fd,
                  eno,
                  strerror_r(eno, errbuf, sizeof(errbuf)));
        rc |= MXB_POLL_HUP;

        if (!dcb->m_dcb_errhandle_called)
        {
            if (dcb_session_check(dcb, "hangup EPOLLHUP"))
            {
                DCB_EH_NOTICE("Calling dcb->m_func.hangup(%p)", dcb);
                dcb->m_func.hangup(dcb);
            }

            dcb->m_dcb_errhandle_called = true;
        }
    }

#ifdef EPOLLRDHUP
    if ((events & EPOLLRDHUP) && (dcb->m_nClose == 0))
    {
        MXB_AT_DEBUG(int eno = gw_getsockerrno(dcb->m_fd));
        MXB_AT_DEBUG(char errbuf[MXS_STRERROR_BUFLEN]);
        MXS_DEBUG("%lu [poll_waitevents] "
                  "EPOLLRDHUP on dcb %p, fd %d. "
                  "Errno %d, %s.",
                  pthread_self(),
                  dcb,
                  dcb->m_fd,
                  eno,
                  strerror_r(eno, errbuf, sizeof(errbuf)));
        rc |= MXB_POLL_HUP;

        if (!dcb->m_dcb_errhandle_called)
        {
            if (dcb_session_check(dcb, "hangup EPOLLRDHUP"))
            {
                DCB_EH_NOTICE("Calling dcb->m_func.hangup(%p)", dcb);
                dcb->m_func.hangup(dcb);
            }

            dcb->m_dcb_errhandle_called = true;
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

    while ((dcb->m_nClose == 0) && (dcb->m_fake_event != 0))
    {
        events = dcb->m_fake_event;
        dcb->m_fake_event = 0;

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
    if (dcb->m_nClose == 0)
    {
        rval = dcb_handler(dcb, events);
    }

    return rval;
}

static bool dcb_is_still_valid(DCB* target, const RoutingWorker& worker)
{
    bool rval = false;

    const auto& dcbs = worker.dcbs();

    if (dcbs.count(target) != 0)
    {
        if (target->m_nClose == 0)
        {
            rval = true;
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
        , m_uid(dcb->m_uid)
    {
    }

    void execute(Worker& worker)
    {
        mxb_assert(&worker == RoutingWorker::get_current());

        RoutingWorker& rworker = static_cast<RoutingWorker&>(worker);
        if (dcb_is_still_valid(m_dcb, rworker) && m_dcb->m_uid == m_uid)
        {
            mxb_assert(m_dcb->owner == RoutingWorker::get_current());
            m_dcb->m_fakeq = m_buffer;
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
    uint64_t m_uid;     /**< DCB UID guarantees we deliver the event to the correct DCB */
};

static void poll_add_event_to_dcb(DCB* dcb, GWBUF* buf, uint32_t ev)
{
    if (dcb == this_thread.current_dcb)
    {
        mxb_assert(dcb->owner == RoutingWorker::get_current());
        // If the fake event is added to the current DCB, we arrange for
        // it to be handled immediately in dcb_handler() when the handling
        // of the current events are done...

        if (dcb->m_fake_event != 0)
        {
            MXS_WARNING("Events have already been injected to current DCB, discarding existing.");
            gwbuf_free(dcb->m_fakeq);
            dcb->m_fake_event = 0;
        }

        dcb->m_fakeq = buf;
        dcb->m_fake_event = ev;
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
    if (dcb->session() || dcb->m_persistentstart)
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

bool DCB::enable_events()
{
    mxb_assert(m_state == DCB_STATE_ALLOC || m_state == DCB_STATE_NOPOLLING);

    bool rv = false;
    RoutingWorker* worker = static_cast<RoutingWorker*>(this->owner);
    mxb_assert(worker == RoutingWorker::get_current());

    if (worker->add_fd(m_fd, THIS_UNIT::poll_events, this))
    {
        m_state = DCB_STATE_POLLING;
        rv = true;
    }

    return rv;
}

bool DCB::disable_events()
{
    mxb_assert(m_state == DCB_STATE_POLLING);
    mxb_assert(m_fd != DCBFD_CLOSED || m_role == DCB::Role::INTERNAL);

    bool rv = true;
    RoutingWorker* worker = static_cast<RoutingWorker*>(this->owner);
    mxb_assert(worker == RoutingWorker::get_current());

    // We unconditionally set the state, even if the actual removal might fail.
    m_state = DCB_STATE_NOPOLLING;

    // When BLR creates an internal DCB, it will set its state to
    // DCB_STATE_NOPOLLING and the fd will be DCBFD_CLOSED.
    if (m_fd != DCBFD_CLOSED)
    {

        if (!worker->remove_fd(m_fd))
        {
            rv = false;
        }
    }

    return rv;
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
    DCB* client_dcb = dcb->session()->client_dcb;
    mxb::Worker* worker = static_cast<mxb::Worker*>(client_dcb->owner);

    // The fd is removed manually here due to the fact that poll_add_dcb causes the DCB to be added to the
    // worker's list of DCBs but poll_remove_dcb doesn't remove it from it. This is due to the fact that the
    // DCBs are only removed from the list when they are closed.
    if (reason == DCB_REASON_HIGH_WATER)
    {
        MXS_INFO("High water mark hit for '%s'@'%s', not reading data until low water mark is hit",
                 client_dcb->m_user, client_dcb->m_remote);

        client_dcb->disable_events();
    }
    else if (reason == DCB_REASON_LOW_WATER)
    {
        MXS_INFO("Low water mark hit for '%s'@'%s', accepting new data", client_dcb->m_user,
                 client_dcb->m_remote);

        if (!client_dcb->enable_events())
        {
            MXS_ERROR("Could not re-enable I/O events for client connection whose I/O events "
                      "earlier were disabled due to the high water mark having been hit. "
                      "Closing session.");
            poll_fake_hangup_event(client_dcb);
        }
    }

    return 0;
}

bool backend_dcb_remove_func(DCB* dcb, void* data)
{
    MXS_SESSION* session = (MXS_SESSION*)data;

    if (dcb->session() == session && dcb->role() == DCB::Role::BACKEND)
    {
        DCB* client_dcb = dcb->session()->client_dcb;
        MXS_INFO("High water mark hit for connection to '%s' from %s'@'%s', not reading data until low water "
                 "mark is hit", dcb->m_server->name(), client_dcb->m_user, client_dcb->m_remote);

        dcb->disable_events();
    }

    return true;
}

bool backend_dcb_add_func(DCB* dcb, void* data)
{
    MXS_SESSION* session = (MXS_SESSION*)data;

    if (dcb->session() == session && dcb->role() == DCB::Role::BACKEND)
    {
        DCB* client_dcb = dcb->session()->client_dcb;
        MXS_INFO("Low water mark hit for connection to '%s' from '%s'@'%s', accepting new data",
                 dcb->m_server->name(), client_dcb->m_user, client_dcb->m_remote);

        if (!dcb->enable_events())
        {
            MXS_ERROR("Could not re-enable I/O events for backend connection whose I/O events "
                      "earlier were disabled due to the high water mark having been hit. "
                      "Closing session.");
            poll_fake_hangup_event(client_dcb);
        }
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
        dcb_foreach_local(backend_dcb_remove_func, dcb->session());
    }
    else if (reason == DCB_REASON_LOW_WATER)
    {
        dcb_foreach_local(backend_dcb_add_func, dcb->session());
    }

    return 0;
}

json_t* dcb_to_json(DCB* dcb)
{
    json_t* obj = json_object();

    char buf[25];
    snprintf(buf, sizeof(buf), "%p", dcb);
    json_object_set_new(obj, "id", json_string(buf));
    json_object_set_new(obj, "server", json_string(dcb->m_server->name()));

    if (dcb->m_func.diagnostics_json)
    {
        json_t* json = dcb->m_func.diagnostics_json(dcb);
        mxb_assert(json);
        json_object_set_new(obj, "protocol_diagnostics", json);
    }

    return obj;
}

namespace maxscale
{

const char* to_string(DCB::Role role)
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

}

SERVICE* DCB::service() const
{
    return m_session->service;
}

void DCB::shutdown()
{
    if (m_role == DCB::Role::CLIENT
        && (m_session->state() == MXS_SESSION::State::STARTED
            || m_session->state() == MXS_SESSION::State::STOPPING))
    {
        session_close(m_session);
    }
}

ClientDCB::ClientDCB(MXS_SESSION* session, DCB::Registry* registry)
    : DCB(DCB::Role::CLIENT, session, nullptr, registry)
{
}

InternalDCB::InternalDCB(MXS_SESSION* session, DCB::Registry* registry)
    : DCB(DCB::Role::INTERNAL, session, nullptr, registry)
{
}

int InternalDCB::ssl_handshake()
{
    mxb_assert(!true);
    return -1;
}

BackendDCB::BackendDCB(MXS_SESSION* session, SERVER* server, DCB::Registry* registry)
    : DCB(DCB::Role::BACKEND, session, server, registry)
{
}

namespace maxscale
{

const char* to_string(dcb_state_t state)
{
    switch (state)
    {
    case DCB_STATE_ALLOC:
        return "DCB_STATE_ALLOC";

    case DCB_STATE_POLLING:
        return "DCB_STATE_POLLING";

    case DCB_STATE_DISCONNECTED:
        return "DCB_STATE_DISCONNECTED";

    case DCB_STATE_NOPOLLING:
        return "DCB_STATE_NOPOLLING";

    default:
        assert(!true);
        return "DCB_STATE_UNKNOWN";
    }
}

}
