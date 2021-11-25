/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-11-19
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
#include <maxscale/dcb.hh>

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

#ifdef OPENSSL_1_1
#include <openssl/x509v3.h>
#endif

#include <atomic>

#include <maxbase/alloc.h>
#include <maxbase/atomic.h>
#include <maxbase/atomic.hh>
#include <maxscale/clock.h>
#include <maxscale/listener.hh>
#include <maxscale/mainworker.hh>
#include <maxscale/protocol/mariadb/mysql.hh>
#include <maxscale/protocol2.hh>
#include <maxscale/router.hh>
#include <maxscale/routingworker.hh>
#include <maxscale/service.hh>
#include <maxscale/utils.h>

#include "internal/modules.hh"
#include "internal/server.hh"
#include "internal/session.hh"

using maxscale::RoutingWorker;
using maxbase::Worker;
using std::string;
using mxs::ClientConnection;
using mxs::BackendConnection;

#define DCB_BELOW_LOW_WATER(x)    ((x)->m_low_water && (x)->m_writeqlen < (x)->m_low_water)
#define DCB_ABOVE_HIGH_WATER(x)   ((x)->m_high_water && (x)->m_writeqlen > (x)->m_high_water)
#define DCB_THROTTLING_ENABLED(x) ((x)->m_high_water && (x)->m_low_water)

namespace
{

static struct THIS_UNIT
{
    std::atomic<uint64_t> uid_generator {0};
#ifdef EPOLLRDHUP
    static constexpr uint32_t poll_events = EPOLLIN | EPOLLOUT | EPOLLRDHUP | EPOLLHUP | EPOLLET;
#else
    static constexpr uint32_t poll_events = EPOLLIN | EPOLLOUT | EPOLLHUP | EPOLLET;
#endif
} this_unit;

static thread_local struct
{
    DCB* current_dcb;       /** The DCB currently being handled by event handlers. */
} this_thread;

/**
 * Create a low level connection to a server.
 *
 * @param host The host to connect to
 * @param port The port to connect to
 *
 * @return File descriptor. Negative on failure.
 */
int connect_socket(const char* host, int port)
{
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
        if (::connect(so, (struct sockaddr*)&addr, sz) == -1 && errno != EINPROGRESS)
        {
            MXS_ERROR("Failed to connect backend server [%s]:%d due to: %d, %s.",
                      host, port, errno, mxs_strerror(errno));
            ::close(so);
            so = -1;
        }
    }
    else
    {
        MXS_ERROR("Establishing connection to backend server [%s]:%d failed.", host, port);
    }
    return so;
}
}

static inline bool dcb_write_parameter_check(DCB* dcb, int fd, GWBUF* queue);
static int         dcb_read_no_bytes_available(DCB* dcb, int fd, int nreadtotal);
static int         dcb_set_socket_option(int sockfd, int level, int optname, void* optval, socklen_t optlen);

static int upstream_throttle_callback(DCB* dcb, DCB::Reason reason, void* userdata);
static int downstream_throttle_callback(DCB* dcb, DCB::Reason reason, void* userdata);

static mxb::WORKER* get_dcb_owner()
{
    /** The DCB is owned by the thread that allocates it */
    mxb_assert(RoutingWorker::get_current_id() != -1);
    return RoutingWorker::get_current();
}

DCB::DCB(int fd,
         const std::string& remote,
         Role role,
         MXS_SESSION* session,
         Handler* handler,
         Manager* manager)
    : POLL_DATA{&DCB::poll_handler, get_dcb_owner()}
    , m_uid(this_unit.uid_generator.fetch_add(1, std::memory_order_relaxed))
    , m_fd(fd)
    , m_role(role)
    , m_remote(remote)
    , m_client_remote(session->client_remote())
    , m_session(session)
    , m_handler(handler)
    , m_manager(manager)
    , m_high_water(config_writeq_high_water())
    , m_low_water(config_writeq_low_water())
{
    auto now = mxs_clock();
    m_last_read = now;
    m_last_write = now;

    if (m_manager)
    {
        m_manager->add(this);
    }
}

DCB::~DCB()
{
    if (this_thread.current_dcb == this)
    {
        this_thread.current_dcb = nullptr;
    }

    if (m_manager)
    {
        m_manager->remove(this);
    }

    remove_callbacks();

    if (m_encryption.handle)
    {
        SSL_free(m_encryption.handle);
    }

    gwbuf_free(m_writeq);
    gwbuf_free(m_readq);

    POLL_DATA::owner = reinterpret_cast<mxb::WORKER*>(0xdeadbeef);
}

void DCB::clear()
{
    gwbuf_free(m_readq);
    gwbuf_free(m_writeq);
    m_readq = NULL;
    m_writeq = NULL;

    remove_callbacks();

    if (m_session)
    {
        release_from(m_session);
        m_session = nullptr;
    }
}

/**
 * Free a DCB and remove it from the chain of all DCBs
 *
 * @param dcb The DCB to free
 */
// static
void DCB::free(DCB* dcb)
{
    mxb_assert(dcb->m_state == State::DISCONNECTED || dcb->m_state == State::CREATED);

    if (dcb->m_session)
    {
        MXS_SESSION* session = dcb->m_session;
        dcb->m_session = NULL;

        if (dcb->release_from(session))
        {
            delete dcb;
        }
    }
    else
    {
        delete dcb;
    }
}

/**
 * Remove a DCB from the poll list and trigger shutdown mechanisms.
 *
 * @param       dcb     The DCB to be processed
 */
void DCB::stop_polling_and_shutdown()
{
    disable_events();
    shutdown();
}

DCB::ReadResult DCB::read(uint32_t min_bytes, uint32_t max_bytes)
{
    mxb_assert(max_bytes >= min_bytes || max_bytes == 0);
    ReadResult rval;
    GWBUF* read_buffer = nullptr;
    int ret = read(&read_buffer, max_bytes);
    if (ret > 0)
    {
        if ((uint32_t)ret >= min_bytes)
        {
            // Enough data.
            rval.data.reset(read_buffer);
            rval.status = ReadResult::Status::READ_OK;
        }
        else
        {
            // Not enough data, save any read data to readq.
            readq_prepend(read_buffer);
            rval.status = ReadResult::Status::INSUFFICIENT_DATA;
        }
    }
    else if (ret == 0)
    {
        rval.status = ReadResult::Status::INSUFFICIENT_DATA;
    }
    return rval;
}

int DCB::read(GWBUF** head, int maxbytes)
{
    mxb_assert(this->owner == RoutingWorker::get_current());
    mxb_assert(m_fd != FD_CLOSED);

    if (m_fd == FD_CLOSED)
    {
        MXS_ERROR("Read failed, dcb is closed.");
        return -1;
    }

    int nsingleread = 0;
    int nreadtotal = 0;

    if (m_readq)
    {
        *head = gwbuf_append(*head, m_readq);
        m_readq = NULL;
        nreadtotal = gwbuf_length(*head);
    }

    if (m_encryption.state == SSLState::ESTABLISHED)
    {
        int n = read_SSL(head);

        if (n < 0)
        {
            if (nreadtotal != 0)
            {
                // TODO: There was something in m_readq, but the SSL
                // TODO: operation failed. We will now return -1 but whatever data was
                // TODO: in m_readq is now in head.
                // TODO: Don't know if this can happen in practice.
                MXS_ERROR("SSL reading failed when existing data already had been "
                          "appended to returned buffer.");
            }

            nreadtotal = -1;
        }
        else
        {
            nreadtotal += n;
        }

        return nreadtotal;
    }

    while (0 == maxbytes || nreadtotal < maxbytes)
    {
        int bytes_available;

        bytes_available = socket_bytes_readable();
        if (bytes_available <= 0)
        {
            return bytes_available < 0 ? -1
                                       :/** Handle closed client socket */
                   dcb_read_no_bytes_available(this, m_fd, nreadtotal);
        }
        else
        {
            GWBUF* buffer;

            buffer = basic_read(bytes_available, maxbytes, nreadtotal, &nsingleread);
            if (buffer)
            {
                m_last_read = mxs_clock();
                nreadtotal += nsingleread;
                MXS_DEBUG("Read %d bytes from dcb %p in state %s fd %d.",
                          nsingleread,
                          this,
                          mxs::to_string(m_state),
                          m_fd);

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

int DCB::socket_bytes_readable() const
{
    int bytesavailable;

    if (-1 == ioctl(m_fd, FIONREAD, &bytesavailable))
    {
        MXS_ERROR("ioctl FIONREAD for dcb %p in state %s fd %d failed: %d, %s",
                  this,
                  mxs::to_string(m_state),
                  m_fd,
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
 * @param fd            File descriptor.
 * @param nreadtotal    Number of bytes that have been read
 * @return              -1 on error, 0 for conditions not treated as error
 */
static int dcb_read_no_bytes_available(DCB* dcb, int fd, int nreadtotal)
{
    /** Handle closed client socket */
    if (nreadtotal == 0 && DCB::Role::CLIENT == dcb->role())
    {
        char c;
        int l_errno = 0;
        long r = -1;

        /* try to read 1 byte, without consuming the socket buffer */
        r = recv(fd, &c, sizeof(char), MSG_PEEK);
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
GWBUF* DCB::basic_read(int bytesavailable, int maxbytes, int nreadtotal, int* nsingleread)
{
    GWBUF* buffer;
    int bufsize = maxbytes == 0 ? bytesavailable : MXS_MIN(bytesavailable, maxbytes - nreadtotal);

    if ((buffer = gwbuf_alloc(bufsize)) == NULL)
    {
        *nsingleread = -1;
    }
    else
    {
        *nsingleread = ::read(m_fd, GWBUF_DATA(buffer), bufsize);
        m_stats.n_reads++;

        if (*nsingleread <= 0)
        {
            if (errno != 0 && errno != EAGAIN && errno != EWOULDBLOCK)
            {
                MXS_ERROR("Read failed, dcb %p in state %s fd %d: %d, %s",
                          this,
                          mxs::to_string(m_state),
                          m_fd,
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
    mxb_assert(m_fd != FD_CLOSED);

    GWBUF* buffer;
    int nsingleread = 0, nreadtotal = 0;
    int start_length = *head ? gwbuf_length(*head) : 0;

    if (m_encryption.write_want_read)
    {
        writeq_drain();
    }

    buffer = basic_read_SSL(&nsingleread);
    if (buffer)
    {
        nreadtotal += nsingleread;
        *head = gwbuf_append(*head, buffer);

        while (buffer)
        {
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

    *nsingleread = SSL_read(m_encryption.handle, temp_buffer, MXS_SO_RCVBUF_SIZE);

    if (*nsingleread)
    {
        m_last_read = mxs_clock();
    }

    m_stats.n_reads++;

    switch (SSL_get_error(m_encryption.handle, *nsingleread))
    {
    case SSL_ERROR_NONE:
        /* Successful read */
        if (*nsingleread && (buffer = gwbuf_alloc_and_load(*nsingleread, (void*)temp_buffer)) == NULL)
        {
            *nsingleread = -1;
            return NULL;
        }
        /* If we were in a retry situation, need to clear flag and attempt write */
        if (m_encryption.read_want_write || m_encryption.read_want_read)
        {
            m_encryption.read_want_write = false;
            m_encryption.read_want_read = false;
            writeq_drain();
        }
        break;

    case SSL_ERROR_ZERO_RETURN:
        /* react to the SSL connection being closed */
        trigger_hangup_event();
        *nsingleread = 0;
        break;

    case SSL_ERROR_WANT_READ:
        /* Prevent SSL I/O on connection until retried, return to poll loop */
        m_encryption.read_want_write = false;
        m_encryption.read_want_read = true;
        *nsingleread = 0;
        break;

    case SSL_ERROR_WANT_WRITE:
        /* Prevent SSL I/O on connection until retried, return to poll loop */
        m_encryption.read_want_write = true;
        m_encryption.read_want_read = false;
        *nsingleread = 0;
        break;

    case SSL_ERROR_SYSCALL:
        *nsingleread = log_errors_SSL(*nsingleread);
        break;

    default:
        *nsingleread = log_errors_SSL(*nsingleread);
        break;
    }
    return buffer;
}

/**
 * Log errors from an SSL operation
 *
 * @param dcb       The DCB of the client
 * @param fd        The file descriptor.
 * @param ret       Return code from SSL operation if error is SSL_ERROR_SYSCALL
 * @return          -1 if an error found, 0 if no error found
 */
int DCB::log_errors_SSL(int ret)
{
    char errbuf[MXS_STRERROR_BUFLEN];
    unsigned long ssl_errno;

    ssl_errno = ERR_get_error();
    if (0 == ssl_errno || m_silence_errors)
    {
        return 0;
    }
    if (ret || ssl_errno)
    {
        MXS_ERROR("SSL operation failed, %s at '%s' in state "
                  "%s fd %d return code %d. More details may follow.",
                  mxs::to_string(m_role),
                  client_remote().c_str(),
                  mxs::to_string(m_state),
                  m_fd,
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

bool DCB::writeq_append(GWBUF* queue, Drain drain)
{
    mxb_assert(this->owner == RoutingWorker::get_current());
    m_writeqlen += gwbuf_length(queue);
    // The following guarantees that queue is not NULL
    if (!dcb_write_parameter_check(this, m_fd, queue))
    {
        return 0;
    }

    m_writeq = gwbuf_append(m_writeq, queue);
    m_stats.n_buffered++;

    if (drain == Drain::YES)
    {
        writeq_drain();
    }

    if (DCB_ABOVE_HIGH_WATER(this) && !m_high_water_reached)
    {
        call_callback(Reason::HIGH_WATER);
        m_high_water_reached = true;
        m_stats.n_high_water++;
    }

    return 1;
}

/**
 * Check the parameters for dcb_write
 *
 * @param dcb   The DCB of the client
 * @param fd    The file descriptor.
 * @param queue Queue of buffers to write
 * @return true if parameters acceptable, false otherwise
 */
static inline bool dcb_write_parameter_check(DCB* dcb, int fd, GWBUF* queue)
{
    if (queue == NULL)
    {
        return false;
    }

    if (fd == DCB::FD_CLOSED)
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
        if (dcb->state() != DCB::State::CREATED
            && dcb->state() != DCB::State::POLLING
            && dcb->state() != DCB::State::NOPOLLING)
        {
            MXS_DEBUG("Write aborted to dcb %p because it is in state %s",
                      dcb,
                      mxs::to_string(dcb->state()));
            gwbuf_free(queue);
            return false;
        }
    }
    return true;
}

int DCB::writeq_drain()
{
    mxb_assert(this->owner == RoutingWorker::get_current());

    if (m_encryption.read_want_write)
    {
        /** The SSL library needs to write more data */
        this->trigger_read_event();
    }

    if (m_writeq == nullptr)
    {
        return 0;
    }

    int total_written = 0;
    GWBUF* local_writeq = m_writeq;
    m_writeq = NULL;

    while (local_writeq)
    {
        int written;
        bool stop_writing = false;
        /* The value put into written will be >= 0 */
        if (m_encryption.handle)
        {
            written = socket_write_SSL(local_writeq, &stop_writing);
        }
        else
        {
            written = socket_write(local_writeq, &stop_writing);
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

    mxb_assert(m_writeqlen >= (uint32_t)total_written);
    m_writeqlen -= total_written;

    if (m_high_water_reached && DCB_BELOW_LOW_WATER(this))
    {
        call_callback(Reason::LOW_WATER);
        m_high_water_reached = false;
        m_stats.n_low_water++;
    }

    return total_written;
}

void DCB::destroy()
{
#if defined (SS_DEBUG)
    RoutingWorker* current = RoutingWorker::get_current();
    RoutingWorker* owner = static_cast<RoutingWorker*>(this->owner);
    if (current && (current != owner))
    {
        MXS_ALERT("dcb_final_close(%p) called by %d, owned by %d.",
                  this,
                  current->id(),
                  owner->id());
        mxb_assert(owner == current);
    }
#endif
    mxb_assert(!m_open);

    if (m_state == State::POLLING)
    {
        stop_polling_and_shutdown();
    }

    if (m_fd != FD_CLOSED)
    {
        // TODO: How could we get this far with a dcb->m_fd <= 0?

        if (::close(m_fd) < 0)
        {
            int eno = errno;
            errno = 0;
            MXS_ERROR("Failed to close socket %d on dcb %p: %d, %s",
                      m_fd,
                      this,
                      eno,
                      mxs_strerror(eno));
        }
        else
        {
            MXS_DEBUG("Closed socket %d on dcb %p.", m_fd, this);
        }

        m_fd = FD_CLOSED;
    }

    m_state = State::DISCONNECTED;
    DCB::free(this);
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
int DCB::socket_write_SSL(GWBUF* writeq, bool* stop_writing)
{
    int written;

    written = SSL_write(m_encryption.handle, GWBUF_DATA(writeq), gwbuf_link_length(writeq));

    *stop_writing = false;
    switch ((SSL_get_error(m_encryption.handle, written)))
    {
    case SSL_ERROR_NONE:
        /* Successful write */
        m_encryption.write_want_read = false;
        m_encryption.write_want_write = false;
        break;

    case SSL_ERROR_ZERO_RETURN:
        /* react to the SSL connection being closed */
        *stop_writing = true;
        trigger_hangup_event();
        break;

    case SSL_ERROR_WANT_READ:
        /* Prevent SSL I/O on connection until retried, return to poll loop */
        *stop_writing = true;
        m_encryption.write_want_read = true;
        m_encryption.write_want_write = false;
        break;

    case SSL_ERROR_WANT_WRITE:
        /* Prevent SSL I/O on connection until retried, return to poll loop */
        *stop_writing = true;
        m_encryption.write_want_read = false;
        m_encryption.write_want_write = true;
        break;

    case SSL_ERROR_SYSCALL:
        *stop_writing = true;
        if (log_errors_SSL(written) < 0)
        {
            trigger_hangup_event();
        }
        break;

    default:
        /* Report error(s) and shutdown the connection */
        *stop_writing = true;
        if (log_errors_SSL(written) < 0)
        {
            trigger_hangup_event();
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
int DCB::socket_write(GWBUF* writeq, bool* stop_writing)
{
    int written = 0;
    int fd = m_fd;
    size_t nbytes = gwbuf_link_length(writeq);
    void* buf = GWBUF_DATA(writeq);
    int saved_errno;
    mxb_assert(nbytes > 0);

    errno = 0;

    if (fd != FD_CLOSED)
    {
        written = ::write(fd, buf, nbytes);
    }

    saved_errno = errno;
    errno = 0;

    if (written < 0)
    {
        *stop_writing = true;
        if (saved_errno != EAGAIN && saved_errno != EWOULDBLOCK && saved_errno != EPIPE && !m_silence_errors)
        {
            MXS_ERROR("Write to %s %s in state %s failed: %d, %s",
                      mxs::to_string(m_role),
                      m_remote.c_str(),
                      mxs::to_string(m_state),
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

bool DCB::add_callback(Reason reason,
                       int (* callback)(DCB*, Reason, void*),
                       void* userdata)
{
    CALLBACK* cb;
    CALLBACK* ptr;
    CALLBACK* lastcb = NULL;

    if ((ptr = (CALLBACK*)MXS_MALLOC(sizeof(CALLBACK))) == NULL)
    {
        return false;
    }
    ptr->reason = reason;
    ptr->cb = callback;
    ptr->userdata = userdata;
    ptr->next = NULL;
    cb = m_callbacks;

    while (cb)
    {
        if (cb->reason == reason && cb->cb == callback
            && cb->userdata == userdata)
        {
            /* Callback is a duplicate, abandon it */
            MXS_FREE(ptr);
            return false;
        }
        lastcb = cb;
        cb = cb->next;
    }
    if (NULL == lastcb)
    {
        m_callbacks = ptr;
    }
    else
    {
        lastcb->next = ptr;
    }

    return true;
}

bool DCB::remove_callback(Reason reason,
                          int (* callback)(DCB*, Reason, void*),
                          void* userdata)
{
    bool rval = false;

    CALLBACK* pcb = NULL;
    CALLBACK* cb = m_callbacks;

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
                m_callbacks = cb->next;
            }

            MXS_FREE(cb);
            rval = true;
            break;
        }
        pcb = cb;
        cb = cb->next;
    }

    return rval;
}

void DCB::remove_callbacks()
{
    while (m_callbacks)
    {
        CALLBACK* cb = m_callbacks;
        m_callbacks = m_callbacks->next;
        MXS_FREE(cb);
    }
}

/**
 * Call the set of callbacks registered for a particular reason.
 *
 * @param dcb           The DCB to call the callbacks regarding
 * @param reason        The reason that has triggered the call
 */
void DCB::call_callback(Reason reason)
{
    CALLBACK* cb;
    CALLBACK* nextcb;
    cb = m_callbacks;

    while (cb)
    {
        if (cb->reason == reason)
        {
            nextcb = cb->next;
            cb->cb(this, reason, cb->userdata);
            cb = nextcb;
        }
        else
        {
            cb = cb->next;
        }
    }
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

/**
 * Create the SSL structure for this DCB.
 * This function creates the SSL structure for the given SSL context.
 * This context should be the context of the service.
 * @param       dcb
 * @return      True on success, false on error.
 */
bool DCB::create_SSL(const mxs::SSLContext& ssl)
{
    m_encryption.verify_host = ssl.config().verify_host;
    m_encryption.handle = ssl.open();
    if (!m_encryption.handle)
    {
        MXS_ERROR("Failed to initialize SSL for connection.");
        return false;
    }

    if (SSL_set_fd(m_encryption.handle, m_fd) == 0)
    {
        MXS_ERROR("Failed to set file descriptor for SSL connection.");
        return false;
    }

    return true;
}

bool DCB::verify_peer_host()
{
    bool rval = true;
#ifdef OPENSSL_1_1
    if (m_encryption.verify_host)
    {
        auto r = remote();
        X509* cert = SSL_get_peer_certificate(m_encryption.handle);

        if (cert)
        {
            if (X509_check_ip_asc(cert, r.c_str(), 0) != 1
                && X509_check_host(cert, r.c_str(), 0, 0, nullptr) != 1)
            {
                char buf[1024] = "";
                X509_NAME_oneline(X509_get_subject_name(cert), buf, sizeof(buf));
                MXS_ERROR("Peer host '%s' does not match certificate: %s", r.c_str(), buf);
                rval = false;
            }

            X509_free(cert);
        }
    }
#endif

    return rval;
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

    void execute(Worker& worker) override final
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
                /**
                 *  TODO: Fix this. m_persistentstart is now in BackendDCB.
                 *  mxb_assert_message(dcb->m_persistentstart > 0, "The DCB must be in a connection pool");
                 */
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

uint32_t DCB::process_events(uint32_t events)
{
    mxb_assert(static_cast<RoutingWorker*>(this->owner) == RoutingWorker::get_current());

    uint32_t rc = mxb::poll_action::NOP;

    /*
     * It isn't obvious that this is impossible
     * mxb_assert(dcb->state() != State::DISCONNECTED);
     */
    if (State::DISCONNECTED == m_state)
    {
        mxb_assert(!true);
        return rc;
    }

    if (!m_open)
    {
        mxb_assert(!true);
        return rc;
    }

    /**
     * Any of these callbacks might close the DCB. Hence, the value of 'n_close'
     * must be checked after each callback invocation.
     *
     * The order in which the events are processed is meaningful and should not be changed. EPOLLERR is
     * handled first to get the best possible error message in the log message in case EPOLLERR is returned
     * with another event from epoll_wait. EPOLLOUT and EPOLLIN are processed before EPOLLHUP and EPOLLRDHUP
     * so that all client events are processed in case EPOLLIN and EPOLLRDHUP events arrive in the same
     * epoll_wait.
     */

    if ((events & EPOLLERR) && (m_open))
    {
        mxb_assert(m_handler);

        rc |= mxb::poll_action::ERROR;

        m_handler->error(this);
    }

    if ((events & EPOLLOUT) && (m_open))
    {
        mxb_assert(m_handler);

        rc |= mxb::poll_action::WRITE;

        m_handler->write_ready(this);
    }

    if ((events & EPOLLIN) && (m_open))
    {
        mxb_assert(m_handler);

        rc |= mxb::poll_action::READ;

        int return_code = 1;
        /** SSL authentication is still going on, we need to call DCB::ssl_handehake
         * until it return 1 for success or -1 for error */
        if (m_encryption.state == SSLState::HANDSHAKE_REQUIRED)
        {
            return_code = ssl_handshake();
        }
        if (1 == return_code)
        {
            m_handler->ready_for_reading(this);
        }
        else if (-1 == return_code)
        {
            m_handler->error(this);
        }
    }

    if ((events & EPOLLHUP) && (m_open))
    {
        mxb_assert(m_handler);

        rc |= mxb::poll_action::HUP;

        if (!m_hanged_up)
        {
            m_handler->hangup(this);

            m_hanged_up = true;
        }
    }

#ifdef EPOLLRDHUP
    if ((events & EPOLLRDHUP) && (m_open))
    {
        mxb_assert(m_handler);

        rc |= mxb::poll_action::HUP;

        if (!m_hanged_up)
        {
            m_handler->hangup(this);

            m_hanged_up = true;
        }
    }
#endif

    if (m_session)
    {
        // By design we don't distinguish between real I/O activity and
        // fake activity. In both cases, the session is busy.
        static_cast<Session*>(m_session)->book_io_activity();
    }

    return rc;
}

// static
uint32_t DCB::event_handler(DCB* dcb, uint32_t events)
{
    this_thread.current_dcb = dcb;
    uint32_t rv = dcb->process_events(events);

    // When all I/O events have been handled, we will immediately
    // process an added fake event. As the handling of a fake event
    // may lead to the addition of another fake event we loop until
    // there is no fake event or the dcb has been closed.

    while ((dcb->m_open) && (dcb->m_triggered_event != 0))
    {
        events = dcb->m_triggered_event;
        dcb->m_triggered_event = 0;

        dcb->m_is_fake_event = true;
        rv |= dcb->process_events(events);
        dcb->m_is_fake_event = false;
    }

    this_thread.current_dcb = NULL;

    return rv;
}

// static
uint32_t DCB::poll_handler(POLL_DATA* data, mxb::WORKER* worker, uint32_t events)
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
    if (dcb->m_open)
    {
        rval = DCB::event_handler(dcb, events);
    }

    return rval;
}

class DCB::FakeEventTask : public Worker::DisposableTask
{
public:
    FakeEventTask(const FakeEventTask&) = delete;
    FakeEventTask& operator=(const FakeEventTask&) = delete;

    FakeEventTask(DCB* dcb, uint32_t ev)
        : m_dcb(dcb)
        , m_ev(ev)
        , m_uid(dcb->uid())
    {
    }

    void execute(Worker& worker) override final
    {
        mxb_assert(&worker == RoutingWorker::get_current());

        RoutingWorker& rworker = static_cast<RoutingWorker&>(worker);

        if (rworker.dcbs().count(m_dcb) != 0    // If the dcb is found in the book-keeping,
            && m_dcb->is_open()                 // it has not been closed, and
            && m_dcb->uid() == m_uid)           // it really is the one (not another one that just
                                                // happened to get the same address).
        {
            mxb_assert(m_dcb->owner == RoutingWorker::get_current());
            m_dcb->m_is_fake_event = true;
            DCB::event_handler(m_dcb, m_ev);
            m_dcb->m_is_fake_event = false;
        }
    }

private:
    DCB*     m_dcb;
    uint32_t m_ev;
    uint64_t m_uid;     /**< DCB UID guarantees we deliver the event to the correct DCB */
};

void DCB::add_event(uint32_t ev)
{
    if (this == this_thread.current_dcb)
    {
        mxb_assert(this->owner == RoutingWorker::get_current());
        // If the fake event is added to the current DCB, we arrange for
        // it to be handled immediately in DCB::event_handler() when the handling
        // of the current events are done...

        m_triggered_event = ev;
    }
    else
    {
        // ... otherwise we post the fake event using the messaging mechanism.

        FakeEventTask* task = new(std::nothrow) FakeEventTask(this, ev);

        if (task)
        {
            RoutingWorker* worker = static_cast<RoutingWorker*>(this->owner);
            worker->execute(std::unique_ptr<FakeEventTask>(task), Worker::EXECUTE_QUEUED);
        }
        else
        {
            MXS_OOM();
        }
    }
}

void DCB::trigger_read_event()
{
    add_event(EPOLLIN);
}

void DCB::trigger_hangup_event()
{
#ifdef EPOLLRDHUP
    uint32_t ev = EPOLLRDHUP;
#else
    uint32_t ev = EPOLLHUP;
#endif
    add_event(ev);
}

void DCB::trigger_write_event()
{
    add_event(EPOLLOUT);
}

bool DCB::enable_events()
{
    mxb_assert(m_state == State::CREATED || m_state == State::NOPOLLING);

    bool rv = false;
    RoutingWorker* worker = static_cast<RoutingWorker*>(this->owner);
    mxb_assert(worker == RoutingWorker::get_current());

    if (worker->add_fd(m_fd, THIS_UNIT::poll_events, this))
    {
        m_state = State::POLLING;
        // Add old manually triggered events from before event disabling. epoll seems to trigger on its own
        // once enabled.
        m_triggered_event |= m_triggered_event_old;
        m_triggered_event_old = 0;
        rv = true;
    }
    return rv;
}

bool DCB::disable_events()
{
    mxb_assert(m_state == State::POLLING);
    mxb_assert(m_fd != FD_CLOSED);

    bool rv = true;
    RoutingWorker* worker = static_cast<RoutingWorker*>(this->owner);
    mxb_assert(worker == RoutingWorker::get_current());

    // We unconditionally set the state, even if the actual removal might fail.
    m_state = State::NOPOLLING;

    // When BLR creates an internal DCB, it will set its state to
    // State::NOPOLLING and the fd will be FD_CLOSED.
    if (m_fd != FD_CLOSED)
    {
        // Remove any manually added read events, then remove fd from epoll.
        m_triggered_event_old = m_triggered_event;
        m_triggered_event = 0;
        if (!worker->remove_fd(m_fd))
        {
            rv = false;
        }
    }

    return rv;
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
static int upstream_throttle_callback(DCB* dcb, DCB::Reason reason, void* userdata)
{
    auto session = dcb->session();
    auto client_dcb = session->client_connection()->dcb();

    // The fd is removed manually here due to the fact that poll_add_dcb causes the DCB to be added to the
    // worker's list of DCBs but poll_remove_dcb doesn't remove it from it. This is due to the fact that the
    // DCBs are only removed from the list when they are closed.
    if (reason == DCB::Reason::HIGH_WATER)
    {
        MXS_INFO("High water mark hit for '%s'@'%s', not reading data until low water mark is hit",
                 session->user().c_str(), client_dcb->remote().c_str());

        client_dcb->disable_events();
    }
    else if (reason == DCB::Reason::LOW_WATER)
    {
        MXS_INFO("Low water mark hit for '%s'@'%s', accepting new data",
                 session->user().c_str(), client_dcb->remote().c_str());

        if (!client_dcb->enable_events())
        {
            MXS_ERROR("Could not re-enable I/O events for client connection whose I/O events "
                      "earlier were disabled due to the high water mark having been hit. "
                      "Closing session.");
            client_dcb->trigger_hangup_event();
        }
    }

    return 0;
}

bool backend_dcb_remove_func(DCB* dcb, void* data)
{
    MXS_SESSION* session = (MXS_SESSION*)data;

    if (dcb->session() == session && dcb->role() == DCB::Role::BACKEND)
    {
        BackendDCB* backend_dcb = static_cast<BackendDCB*>(dcb);
        MXS_INFO("High water mark hit for connection to '%s' from %s'@'%s', not reading data until low water "
                 "mark is hit", backend_dcb->server()->name(),
                 session->user().c_str(), session->client_remote().c_str());

        backend_dcb->disable_events();
    }

    return true;
}

bool backend_dcb_add_func(DCB* dcb, void* data)
{
    MXS_SESSION* session = (MXS_SESSION*)data;

    if (dcb->session() == session && dcb->role() == DCB::Role::BACKEND)
    {
        BackendDCB* backend_dcb = static_cast<BackendDCB*>(dcb);
        auto client_dcb = session->client_connection()->dcb();
        MXS_INFO("Low water mark hit for connection to '%s' from '%s'@'%s', accepting new data",
                 backend_dcb->server()->name(),
                 session->user().c_str(), client_dcb->remote().c_str());

        if (!backend_dcb->enable_events())
        {
            MXS_ERROR("Could not re-enable I/O events for backend connection whose I/O events "
                      "earlier were disabled due to the high water mark having been hit. "
                      "Closing session.");
            client_dcb->trigger_hangup_event();
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
static int downstream_throttle_callback(DCB* dcb, DCB::Reason reason, void* userdata)
{
    if (reason == DCB::Reason::HIGH_WATER)
    {
        dcb_foreach_local(backend_dcb_remove_func, dcb->session());
    }
    else if (reason == DCB::Reason::LOW_WATER)
    {
        dcb_foreach_local(backend_dcb_add_func, dcb->session());
    }

    return 0;
}

SERVICE* DCB::service() const
{
    return m_session->service;
}

/**
 * ClientDCB
 */
void ClientDCB::shutdown()
{
    // Close protocol and router session
    if ((m_session->state() == MXS_SESSION::State::STARTED
         || m_session->state() == MXS_SESSION::State::STOPPING))
    {
        m_session->close();
    }
    m_protocol->finish_connection();
}

ClientDCB::ClientDCB(int fd,
                     const std::string& remote,
                     const sockaddr_storage& ip,
                     MXS_SESSION* session,
                     std::unique_ptr<ClientConnection> protocol,
                     DCB::Manager* manager)
    : ClientDCB(fd,
                remote,
                ip,
                DCB::Role::CLIENT,
                session,
                std::move(protocol),
                manager)
{
}

ClientDCB::ClientDCB(int fd,
                     const std::string& remote,
                     const sockaddr_storage& ip,
                     DCB::Role role,
                     MXS_SESSION* session,
                     std::unique_ptr<ClientConnection> protocol,
                     Manager* manager)
    : DCB(fd, remote, role, session, protocol.get(), manager)
    , m_ip(ip)
    , m_protocol(std::move(protocol))
{
    if (DCB_THROTTLING_ENABLED(this))
    {
        add_callback(Reason::HIGH_WATER, downstream_throttle_callback, NULL);
        add_callback(Reason::LOW_WATER, downstream_throttle_callback, NULL);
    }
}

ClientDCB::ClientDCB(int fd, const std::string& remote, DCB::Role role, MXS_SESSION* session)
    : ClientDCB(fd, remote, sockaddr_storage {}, role, session, nullptr, nullptr)
{
}

ClientDCB::~ClientDCB()
{
    // TODO: move m_data to authenticators so it's freed
}

bool ClientDCB::release_from(MXS_SESSION* session)
{
    /**
     * The client DCB is only freed once all other DCBs that the session
     * uses have been freed. This will guarantee that the authentication
     * data will be usable for all DCBs even if the client DCB has already
     * been closed.
     */
    session_put_ref(session);
    return false;
}

ClientDCB* ClientDCB::create(int fd,
                             const std::string& remote,
                             const sockaddr_storage& ip,
                             MXS_SESSION* session,
                             std::unique_ptr<ClientConnection> protocol,
                             DCB::Manager* manager)
{
    ClientDCB* dcb = new(std::nothrow) ClientDCB(fd, remote, ip, session, std::move(protocol), manager);
    if (!dcb)
    {
        ::close(fd);
    }

    return dcb;
}

mxs::ClientConnection* ClientDCB::protocol() const
{
    return m_protocol.get();
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
    if (!m_session->listener_data()->m_ssl.valid()
        || (!m_encryption.handle && !create_SSL(m_session->listener_data()->m_ssl)))
    {
        return -1;
    }

    int ssl_rval = SSL_accept(m_encryption.handle);

    switch (SSL_get_error(m_encryption.handle, ssl_rval))
    {
    case SSL_ERROR_NONE:
        MXS_DEBUG("SSL_accept done for %s", m_remote.c_str());
        m_encryption.state = SSLState::ESTABLISHED;
        m_encryption.read_want_write = false;
        return verify_peer_host() ? 1 : -1;

    case SSL_ERROR_WANT_READ:
        MXS_DEBUG("SSL_accept ongoing want read for %s", m_remote.c_str());
        return 0;

    case SSL_ERROR_WANT_WRITE:
        MXS_DEBUG("SSL_accept ongoing want write for %s", m_remote.c_str());
        m_encryption.read_want_write = true;
        return 0;

    case SSL_ERROR_ZERO_RETURN:
        MXS_DEBUG("SSL error, shut down cleanly during SSL accept %s", m_remote.c_str());
        log_errors_SSL(0);
        trigger_hangup_event();
        return 0;

    case SSL_ERROR_SYSCALL:
        MXS_DEBUG("SSL connection SSL_ERROR_SYSCALL error during accept %s", m_remote.c_str());
        if (log_errors_SSL(ssl_rval) < 0)
        {
            m_encryption.state = SSLState::HANDSHAKE_FAILED;
            trigger_hangup_event();
            return -1;
        }
        else
        {
            return 0;
        }

    default:
        MXS_DEBUG("SSL connection shut down with error during SSL accept %s", m_remote.c_str());
        if (log_errors_SSL(ssl_rval) < 0)
        {
            m_encryption.state = SSLState::HANDSHAKE_FAILED;
            trigger_hangup_event();
            return -1;
        }
        else
        {
            return 0;
        }
    }
}

int ClientDCB::port() const
{
    int rval = -1;

    if (m_ip.ss_family == AF_INET)
    {
        struct sockaddr_in* ip = (struct sockaddr_in*)&m_ip;
        rval = ntohs(ip->sin_port);
    }
    else if (m_ip.ss_family == AF_INET6)
    {
        struct sockaddr_in6* ip = (struct sockaddr_in6*)&m_ip;
        rval = ntohs(ip->sin6_port);
    }
    else
    {
        mxb_assert(m_ip.ss_family == AF_UNIX);
    }

    return rval;
}

void ClientDCB::close(ClientDCB* dcb)
{
    DCB::close(dcb);
}

void DCB::close(DCB* dcb)
{
#if defined (SS_DEBUG)
    mxb_assert(dcb->m_state != State::DISCONNECTED && dcb->m_fd != FD_CLOSED && dcb->m_manager);
    auto* current = RoutingWorker::get_current();
    auto* owner = static_cast<RoutingWorker*>(dcb->owner);
    mxb_assert(current && current == owner);
#endif

    if (dcb->m_open)
    {
        dcb->m_open = false;
        dcb->m_manager->destroy(dcb);
    }
    else
    {
        // TODO: Will this happen on a regular basis?
        MXS_WARNING("DCB::close(%p) called on a closed dcb.", dcb);
        mxb_assert(!true);
    }
}

/**
 * BackendDCB
 */
BackendDCB* BackendDCB::connect(SERVER* server, MXS_SESSION* session, DCB::Manager* manager)
{
    BackendDCB* rval = nullptr;
    // Start the watchdog notifier, the getaddrinfo call done by connect_socket() can take a long time in some
    // corner cases.
    session->worker()->start_watchdog_workaround();
    int fd = connect_socket(server->address(), server->port());
    session->worker()->stop_watchdog_workaround();

    if (fd >= 0)
    {
        rval = new(std::nothrow) BackendDCB(server, fd, session, manager);
        if (!rval)
        {
            ::close(fd);
        }
    }
    return rval;
}

void BackendDCB::reset(MXS_SESSION* session)
{
    m_last_read = mxs_clock();
    m_last_write = mxs_clock();
    m_session = session;

    if (DCB_THROTTLING_ENABLED(this))
    {
        // Register upstream throttling callbacks
        add_callback(Reason::HIGH_WATER, upstream_throttle_callback, NULL);
        add_callback(Reason::LOW_WATER, upstream_throttle_callback, NULL);
    }
}

// static
void BackendDCB::hangup_cb(const SERVER* server)
{
    auto* rworker = RoutingWorker::get_current();
    DCB* old_current = this_thread.current_dcb;

    for (DCB* dcb : rworker->dcbs())
    {
        if (dcb->state() == State::POLLING && dcb->role() == Role::BACKEND)
        {
            // TODO: Remove the need for downcast.
            BackendDCB* backend_dcb = static_cast<BackendDCB*>(dcb);

            if (backend_dcb->m_server == server && backend_dcb->is_open())
            {
                if (!backend_dcb->m_hanged_up)
                {
                    this_thread.current_dcb = backend_dcb;
                    backend_dcb->m_is_fake_event = true;
                    backend_dcb->m_protocol->hangup(dcb);
                    backend_dcb->m_is_fake_event = true;
                    backend_dcb->m_hanged_up = true;
                }
            }
        }
    }

    this_thread.current_dcb = old_current;
}

/**
 * Call all the callbacks on all DCB's that match the server and the reason given
 */
// static
void BackendDCB::hangup(const SERVER* server)
{
    auto hangup_server = [server]() {
            hangup_cb(server);
        };
    mxs::RoutingWorker::broadcast(hangup_server, mxs::RoutingWorker::EXECUTE_QUEUED);
}

mxs::BackendConnection* BackendDCB::protocol() const
{
    return m_protocol.get();
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

    if (!m_ssl || (!m_encryption.handle && !create_SSL(*m_ssl)))
    {
        mxb_assert(m_ssl);
        return -1;
    }
    m_encryption.state = SSLState::HANDSHAKE_REQUIRED;
    ssl_rval = SSL_connect(m_encryption.handle);
    switch (SSL_get_error(m_encryption.handle, ssl_rval))
    {
    case SSL_ERROR_NONE:
        MXS_DEBUG("SSL_connect done for %s", m_remote.c_str());
        m_encryption.state = SSLState::ESTABLISHED;
        m_encryption.read_want_write = false;
        return_code = verify_peer_host() ? 1 : -1;
        break;

    case SSL_ERROR_WANT_READ:
        MXS_DEBUG("SSL_connect ongoing want read for %s", m_remote.c_str());
        return_code = 0;
        break;

    case SSL_ERROR_WANT_WRITE:
        MXS_DEBUG("SSL_connect ongoing want write for %s", m_remote.c_str());
        m_encryption.read_want_write = true;
        return_code = 0;
        break;

    case SSL_ERROR_ZERO_RETURN:
        MXS_DEBUG("SSL error, shut down cleanly during SSL connect %s", m_remote.c_str());
        if (log_errors_SSL(0) < 0)
        {
            trigger_hangup_event();
        }
        return_code = 0;
        break;

    case SSL_ERROR_SYSCALL:
        MXS_DEBUG("SSL connection shut down with SSL_ERROR_SYSCALL during SSL connect %s", m_remote.c_str());
        if (log_errors_SSL(ssl_rval) < 0)
        {
            m_encryption.state = SSLState::HANDSHAKE_FAILED;
            trigger_hangup_event();
            return_code = -1;
        }
        else
        {
            return_code = 0;
        }
        break;

    default:
        MXS_DEBUG("SSL connection shut down with error during SSL connect %s", m_remote.c_str());
        if (log_errors_SSL(ssl_rval) < 0)
        {
            m_encryption.state = SSLState::HANDSHAKE_FAILED;
            trigger_hangup_event();
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

BackendDCB::BackendDCB(SERVER* server, int fd, MXS_SESSION* session,
                       DCB::Manager* manager)
    : DCB(fd, server->address(), DCB::Role::BACKEND, session, nullptr, manager)
    , m_server(server)
    , m_ssl(static_cast<Server*>(server)->ssl())
{
    mxb_assert(m_server);

    if (DCB_THROTTLING_ENABLED(this))
    {
        // Register upstream throttling callbacks
        add_callback(Reason::HIGH_WATER, upstream_throttle_callback, NULL);
        add_callback(Reason::LOW_WATER, upstream_throttle_callback, NULL);
    }
}

void BackendDCB::shutdown()
{
    // Close protocol and router session
    m_protocol->finish_connection();
}

bool BackendDCB::release_from(MXS_SESSION* session)
{
    auto ses = static_cast<Session*>(session);
    ses->unlink_backend_connection(m_protocol.get());
    return true;
}

void BackendDCB::set_connection(std::unique_ptr<mxs::BackendConnection> conn)
{
    m_handler = conn.get();
    m_protocol = std::move(conn);
}

void BackendDCB::close(BackendDCB* dcb)
{
    mxb_assert(dcb->m_state != State::CREATED);
    DCB::close(dcb);
}

BackendDCB::Manager* BackendDCB::manager() const
{
    return static_cast<Manager*>(m_manager);
}

/**
 * Free Functions
 */
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

    default:
        mxb_assert(!true);
        return "Unknown DCB";
    }
}

const char* to_string(DCB::State state)
{
    switch (state)
    {
    case DCB::State::CREATED:
        return "DCB::State::CREATED";

    case DCB::State::POLLING:
        return "DCB::State::POLLING";

    case DCB::State::DISCONNECTED:
        return "DCB::State::DISCONNECTED";

    case DCB::State::NOPOLLING:
        return "DCB::State::NOPOLLING";

    default:
        assert(!true);
        return "DCB::State::UNKNOWN";
    }
}
}

int dcb_count_by_role(DCB::Role role)
{
    struct dcb_role_count val = {};
    val.count = 0;
    val.role = role;

    dcb_foreach(count_by_role_cb, &val);

    return val.count;
}

uint64_t dcb_get_session_id(DCB* dcb)
{
    return (dcb && dcb->session()) ? dcb->session()->id() : 0;
}

bool dcb_foreach(bool (* func)(DCB* dcb, void* data), void* data)
{
    mxb_assert(mxs::MainWorker::is_main_worker());
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
            /**
             *  TODO: Fix this. m_persistentstart is now in BackendDCB.
             *  mxb_assert_message(dcb->m_persistentstart > 0, "The DCB must be in a connection pool");
             */
        }
    }
}

DCB* dcb_get_current()
{
    return this_thread.current_dcb;
}

void mxs::ClientConnectionBase::set_dcb(DCB* dcb)
{
    m_dcb = static_cast<ClientDCB*>(dcb);
}

ClientDCB* mxs::ClientConnectionBase::dcb()
{
    return m_dcb;
}

const ClientDCB* mxs::ClientConnectionBase::dcb() const
{
    return m_dcb;
}

json_t* maxscale::ClientConnectionBase::diagnostics() const
{
    json_t* rval = json_object();   // This is not currently used.
    return rval;
}

bool mxs::ClientConnectionBase::in_routing_state() const
{
    return m_dcb != nullptr;
}

bool DCB::ReadResult::ok() const
{
    return status == Status::READ_OK;
}

bool DCB::ReadResult::error() const
{
    return status == Status::ERROR;
}

DCB::ReadResult::operator bool() const
{
    return ok();
}
