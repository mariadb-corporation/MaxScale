/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-03-14
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

#include <maxbase/alloc.hh>
#include <maxbase/hexdump.hh>
#include <maxscale/clock.hh>
#include <maxscale/cn_strings.hh>
#include <maxscale/listener.hh>
#include <maxscale/mainworker.hh>
#include <maxscale/protocol2.hh>
#include <maxscale/routingworker.hh>
#include <maxscale/service.hh>
#include <maxscale/utils.hh>

#include "internal/server.hh"
#include "internal/session.hh"

using maxscale::RoutingWorker;
using maxbase::Worker;
using std::string;
using std::move;
using mxs::ClientConnection;
using mxs::BackendConnection;

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

/*
 * Unclear which is the optimal read buffer size. LibUV uses 64 kB, so try that here too. This
 * requires 6 GB of memory with 100k GWBUFs in flight. We want to minimize the number of small
 * reads, as even a zero-size read takes 2-3 us. 30 kB read seems to take ~30 us. With larger
 * reads the time increases somewhat linearly, although fluctuations are significant.
 *
 * TODO: Think how this will affect long-term stored GWBUFs (e.g. session commands). They will now
 * consume much more memory.
 */
const size_t BASE_READ_BUFFER_SIZE = 64 * 1024;

void set_SSL_mode_bits(SSL* ssl)
{
    /*
     * SSL mode bits:
     * 1. Partial writes allow consuming from write buffer as writing progresses.
     * 2. Auto-retry is enabled by default on more recent OpenSSL versions. Unclear if it matters for
     * non-blocking sockets.
     * 3. Moving write buffer is required as GWBUF storage may be reallocated before a write retry.
     */
    const long SSL_MODE_BITS = SSL_MODE_ENABLE_PARTIAL_WRITE | SSL_MODE_AUTO_RETRY
        | SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER;

    auto bits = SSL_set_mode(ssl, SSL_MODE_BITS);
    mxb_assert((bits & SSL_MODE_BITS) == SSL_MODE_BITS);
}

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
            MXB_ERROR("Failed to connect backend server [%s]:%d due to: %d, %s.",
                      host, port, errno, mxb_strerror(errno));
            ::close(so);
            so = -1;
        }
    }
    else
    {
        MXB_ERROR("Establishing connection to backend server [%s]:%d failed.", host, port);
    }
    return so;
}
}

static int dcb_read_no_bytes_available(DCB* dcb, int fd, int nreadtotal);
static int dcb_set_socket_option(int sockfd, int level, int optname, void* optval, socklen_t optlen);

static int upstream_throttle_callback(DCB* dcb, DCB::Reason reason, void* userdata);
static int downstream_throttle_callback(DCB* dcb, DCB::Reason reason, void* userdata);

static mxb::Worker* get_dcb_owner()
{
    /** The DCB is owned by the thread that allocates it */
    mxb_assert(RoutingWorker::get_current() != nullptr);
    return RoutingWorker::get_current();
}

DCB::DCB(int fd,
         const std::string& remote,
         Role role,
         MXS_SESSION* session,
         Handler* handler,
         Manager* manager)
    : m_owner(get_dcb_owner())
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
}

void DCB::clear()
{
    m_readq.clear();
    m_writeq.clear();

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

std::tuple<bool, GWBUF> DCB::read(size_t minbytes, size_t maxbytes)
{
    return read_impl(minbytes, maxbytes, ReadLimit::RES_LEN);
}

std::tuple<bool, GWBUF> DCB::read_strict(size_t minbytes, size_t maxbytes)
{
    return read_impl(minbytes, maxbytes, ReadLimit::STRICT);
}

std::tuple<bool, GWBUF> DCB::read_impl(size_t minbytes, size_t maxbytes, ReadLimit limit_type)
{
    mxb_assert(m_owner == RoutingWorker::get_current());
    mxb_assert(m_fd != FD_CLOSED);
    mxb_assert(maxbytes >= minbytes || maxbytes == 0);

    bool read_success = false;
    bool trigger_again = false;

    if (maxbytes > 0 && m_readq.length() >= maxbytes)
    {
        // Already have enough data. May have more (either in readq or in socket), so read again later.
        // Should not happen on first read from the TCP socket (strict limit). Subsequent reads can end up
        // here even if ReadLimit::STRICT is used.
        read_success = true;
        trigger_again = true;
    }
    else if (m_encryption.state == SSLState::ESTABLISHED)
    {
        mxb_assert(limit_type == ReadLimit::RES_LEN);
        if (socket_read_SSL(maxbytes))
        {
            read_success = true;
        }
    }
    else
    {
        if (socket_read(maxbytes, limit_type))
        {
            read_success = true;
        }
    }

    GWBUF rval_buf;
    bool rval_ok = false;

    if (read_success)
    {
        rval_ok = true;
        auto readq_len = m_readq.length();

        MXB_DEBUG("Read %lu bytes from dcb %p (%s) in state %s fd %d.",
                  readq_len, this, whoami().c_str(), mxs::to_string(m_state), m_fd);

        if (maxbytes > 0 && readq_len >= maxbytes)
        {
            // Maxbytes-limit is in effect.
            if (readq_len > maxbytes)
            {
                // Readq has data left after this read, must read again.
                trigger_again = true;
                rval_buf = m_readq.split(maxbytes);
            }
            else
            {
                // If socket has data left, a read has already been scheduled by a lower level function.
                rval_buf = move(m_readq);
            }
        }
        else if ((minbytes > 0 && readq_len >= minbytes) || (minbytes == 0 && readq_len > 0))
        {
            rval_buf = move(m_readq);
        }

        // If there's extra data left after a ReadLimit::STRICT, a read event is not triggered and the caller
        // is responsible for emptying the socket. This prevents the use of DCB::read_strict() followed by
        // a DCB::read() from causing a busy-loop when there's a partial packet in the readq.
        if (trigger_again && limit_type == ReadLimit::RES_LEN)
        {
            trigger_read_event();
        }
    }
    return {rval_ok, move(rval_buf)};
}

int DCB::socket_bytes_readable() const
{
    int bytesavailable;

    if (-1 == ioctl(m_fd, FIONREAD, &bytesavailable))
    {
        MXB_ERROR("ioctl FIONREAD for dcb %p in state %s fd %d failed: %d, %s",
                  this,
                  mxs::to_string(m_state),
                  m_fd,
                  errno,
                  mxb_strerror(errno));
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

inline bool is_incomplete_read(int64_t read_amount)
{
    mxs::Config& global_config = mxs::Config::get();

    return global_config.max_read_amount != 0 && read_amount >= global_config.max_read_amount;
}

/**
 * Basic read function. Reads bytes from socket.
 *
 * @param maxbytes Maximum bytes to read (0 = no limit)
 */
bool DCB::socket_read(size_t maxbytes, ReadLimit limit_type)
{
    bool keep_reading = true;
    bool socket_cleared = false;
    bool success = true;
    size_t bytes_from_socket = 0;
    bool strict_limit = (limit_type == ReadLimit::STRICT);
    mxb_assert(!strict_limit || maxbytes > 0);      // In strict mode a maxbytes limit is mandatory.

    while (keep_reading)
    {
        auto [ptr, read_limit] = strict_limit ? calc_read_limit_strict(maxbytes) :
            m_readq.prepare_to_write(BASE_READ_BUFFER_SIZE);

        auto ret = ::read(m_fd, ptr, read_limit);
        m_stats.n_reads++;
        if (ret > 0)
        {
            MXB_DEBUG("%s\n%s", whoami().c_str(), mxb::hexdump(ptr, ret).c_str());
            m_readq.write_complete(ret);
            bytes_from_socket += ret;
            if (ret < (int64_t)read_limit)
            {
                // According to epoll documentation (questions and answers-section), the socket is now
                // clear. No need to keep reading.
                keep_reading = false;
                socket_cleared = true;
            }
            else if (maxbytes > 0 && m_readq.length() >= maxbytes)
            {
                keep_reading = false;
            }
        }
        else if (ret == 0)
        {
            // Eof, received on disconnect. This can happen repeatedly in an endless loop. Assume that fd is
            // cleared and will trigger epoll if needed.
            keep_reading = false;
            socket_cleared = true;
        }
        else
        {
            int eno = errno;
            if (eno == EAGAIN || eno == EWOULDBLOCK)
            {
                // Done for now, no more can be read.
                keep_reading = false;
                socket_cleared = true;
            }
            else if (eno == EINTR)
            {
                // Just try again.
            }
            else
            {
                // Unexpected error.
                MXB_ERROR("Read from socket fd %i failed. Error %i: %s. Dcb in state %s.",
                          m_fd, eno, mxb_strerror(eno), mxs::to_string(m_state));
                keep_reading = false;
                socket_cleared = true;
                success = false;
            }
        }
    }

    mxb_assert(!strict_limit || m_readq.length() <= maxbytes);      // Strict mode must limit readq length.

    if (success)
    {
        if (bytes_from_socket > 0)
        {
            m_last_read = mxs_clock();      // Empty reads do not delay idle status.
        }
        if (!socket_cleared && !strict_limit)
        {
            trigger_read_event();   // Edge-triggered, so add to ready list as more data may be available.
        }
    }

    m_read_amount += bytes_from_socket;

    if (is_incomplete_read(m_read_amount))
    {
        m_incomplete_read = true;
    }

    return success;
}

/**
 * General purpose read routine to read data from a socket through the SSL
 * structure lined with this DCB. The SSL structure should
 * be initialized and the SSL handshake should be done.
 *
 * @param maxbytes Maximum bytes to read (0 = no limit)
 * @return True on success
 */
bool DCB::socket_read_SSL(size_t maxbytes)
{
    if (m_encryption.write_want_read)
    {
        // A write-operation was waiting for a readable socket. Now that the socket seems readable,
        // retry the write.
        writeq_drain();
    }

    bool keep_reading = true;
    bool socket_cleared = false;
    bool success = true;
    size_t bytes_from_socket = 0;

    // OpenSSL has an internal buffer limit of 16 kB (16384), and will never return more data in a single
    // read.
    const uint64_t openssl_read_limit = 16 * 1024;

    while (keep_reading)
    {
        // On first iteration, reserve plenty of space, similar to normal read. On successive iterations,
        // less space is enough as OpenSSL cannot fill the entire buffer at once anyway. This saves on
        // reallocations when reading multiple 16 kB blocks in succession. GWBUF will still
        // double its size when it needs to reallocate.
        auto [ptr, alloc_limit] = (bytes_from_socket == 0) ? m_readq.prepare_to_write(BASE_READ_BUFFER_SIZE) :
            m_readq.prepare_to_write(openssl_read_limit);

        // In theory, the readq could be larger than INT_MAX bytes.
        int read_limit = std::min(alloc_limit, (size_t)INT_MAX);

        ERR_clear_error();
        auto ret = SSL_read(m_encryption.handle, ptr, read_limit);
        m_stats.n_reads++;
        if (ret > 0)
        {
            m_readq.write_complete(ret);
            bytes_from_socket += ret;

            m_encryption.read_want_write = false;

            if (maxbytes > 0 && m_readq.length() >= maxbytes)
            {
                keep_reading = false;
            }
        }
        else
        {
            keep_reading = false;
            socket_cleared = true;

            switch (SSL_get_error(m_encryption.handle, ret))
            {
            case SSL_ERROR_ZERO_RETURN:
                // SSL-connection closed.
                trigger_hangup_event();
                break;

            case SSL_ERROR_WANT_READ:
                // No more data can be read, return to poll. This is equivalent to EWOULDBLOCK.
                m_encryption.read_want_write = false;
                break;

            case SSL_ERROR_WANT_WRITE:
                // Read-operation needs to write data but socket is not writable. Return to poll,
                // wait for a write-ready-event and then read again.
                m_encryption.read_want_write = true;
                break;

            default:
                success = (log_errors_SSL(ret) >= 0);
                break;
            }
        }
    }

    if (success)
    {
        if (bytes_from_socket > 0)
        {
            m_last_read = mxs_clock();
        }
        if (!socket_cleared)
        {
            trigger_read_event();
        }
    }

    m_read_amount += bytes_from_socket;

    if (is_incomplete_read(m_read_amount))
    {
        m_incomplete_read = true;
    }

    return success;
}

std::string DCB::get_one_SSL_error(unsigned long ssl_errno)
{
    // OpenSSL says the buffer size must be greater than 120 bytes for 1.0.2 but in 1.1.1 it must be greater
    // than 256 (same for 3.0). Using ERR_error_string_n checks the length so this is forwards compatible.
    std::array<char, 256> buf {};
    ERR_error_string_n(ssl_errno, buf.data(), buf.size());
    std::string rval = buf.data();

    if (rval.find("no shared cipher") != std::string::npos)
    {
        // No shared ciphers, print the list of ciphers we offered and, if possible, the ones the client asked
        // for. This should help administrators determine why the failure happened. Usually this happens when
        // an older client attempts to connect to a MaxScale instance that uses a newer TLS version.

#ifdef OPENSSL_1_1
        // This only works when we're acting as the server.
        if (STACK_OF(SSL_CIPHER) * ciphers = SSL_get_client_ciphers(m_encryption.handle))
        {
            rval += " (Client ciphers: ";
            int n = sk_SSL_CIPHER_num(ciphers);

            for (int i = 0; i < n; i++)
            {
                if (i != 0)
                {
                    rval += ":";
                }

                rval += SSL_CIPHER_get_name(sk_SSL_CIPHER_value(ciphers, i));
            }

            rval += ")";
        }
#endif

        rval += " (Our ciphers: ";
        int i = 0;
        std::string ciphers;

        while (const char* c = SSL_get_cipher_list(m_encryption.handle, i))
        {
            if (i != 0)
            {
                rval += ":";
            }

            rval += c;
            ++i;
        }

        rval += ")";
    }

    return rval;
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
    std::ostringstream ss;
    unsigned long ssl_errno = ERR_get_error();

    if (0 == ssl_errno || m_silence_errors)
    {
        return 0;
    }

    if (ret && !ssl_errno)
    {
        ss << "network error (" << errno << ", " << mxb_strerror(errno) << ")";
    }
    else
    {
        ss << get_one_SSL_error(ssl_errno);

        while ((ssl_errno = ERR_get_error()) != 0)
        {
            ss << ", " << get_one_SSL_error(ssl_errno);
        }
    }

    if (ret || ssl_errno)
    {
        MXB_ERROR("SSL operation failed for %s at '%s': %s",
                  mxs::to_string(m_role), client_remote().c_str(), ss.str().c_str());
    }

    return -1;
}

bool DCB::writeq_append(GWBUF* queue)
{
    mxb_assert(queue);
    GWBUF buffer(move(*queue));
    delete queue;
    return writeq_append(move(buffer));
}

bool DCB::writeq_append(GWBUF&& data)
{
    // polling_worker() can not be used here, the last backend write takes place
    // when the DCB has already been removed from the epoll-set.
    mxb_assert(m_owner == RoutingWorker::get_current());

    // This can be slow in a situation where data is queued faster than it can be sent.
    m_writeq.merge_back(move(data));

    mxb_assert_message(m_fd != DCB::FD_CLOSED, "Trying to write to closed socket.");

    if (!m_session || m_session->state() != MXS_SESSION::State::STOPPING)
    {
        /**
         * MXS_SESSION::State::STOPPING means that one of the backends is closing
         * the router session. Some backends may have not completed
         * authentication yet and thus they have no information about router
         * being closed. Session state is changed to MXS_SESSION::State::STOPPING
         * before router's closeSession is called and that tells that DCB may
         * still be writable.
         */
        mxb_assert(m_state != DCB::State::DISCONNECTED);
    }

    m_stats.n_buffered++;
    writeq_drain();

    if (m_high_water > 0 && m_writeq.length() > m_high_water && !m_high_water_reached)
    {
        call_callback(Reason::HIGH_WATER);
        m_high_water_reached = true;
        m_stats.n_high_water++;
    }
    return true;    // Propagate this change to callers once it's clear the asserts are not hit.
}

void DCB::writeq_drain()
{
    // polling_worker() can not be used here, the last backend drain takes place
    // when the DCB has already been removed from the epoll-set.
    mxb_assert(m_owner == RoutingWorker::get_current());

    if (m_encryption.handle)
    {
        if (m_encryption.read_want_write)
        {
            // A read-op was waiting for a writable socket. The socket may be writable now,
            // so retry the read.
            trigger_read_event();
        }
        socket_write_SSL();
    }
    else
    {
        socket_write();
    }

    if (m_writeq.empty())
    {
        /**
         * Writeq has been completely consumed. Take some simple steps to recycle buffers.
         *  Don't try to recycle if:
         *  1. Underlying data is shared or null. Let the last owner recycle it.
         *  2. The allocated buffer is large. The large buffer limit is subject to discussion. This limit
         *  is required to avoid keeping large amounts of memory tied to one GWBUF.
         *
         *  If writeq is suitable, readq is empty and has less capacity than writeq, recycle writeq.
         */

        // TODO: Add smarter way to estimate required readq capacity. E.g. average packet size.
        auto writeq_cap = m_writeq.capacity();
        if (m_writeq.is_unique() && writeq_cap > 0 && writeq_cap <= BASE_READ_BUFFER_SIZE
            && m_readq.empty() && m_readq.capacity() < writeq_cap)
        {
            m_writeq.reset();
            m_readq = std::move(m_writeq);
        }
        else
        {
            // Would end up happening later on anyway, best to clear now. If the underlying data was
            // shared the other owner may become unique and won't need to allocate when writing.
            m_writeq.clear();
        }
    }

    if (m_high_water_reached && m_writeq.length() <= m_low_water)
    {
        call_callback(Reason::LOW_WATER);
        m_high_water_reached = false;
        m_stats.n_low_water++;
    }
}

void DCB::destroy()
{
#if defined (SS_DEBUG)
    RoutingWorker* current = RoutingWorker::get_current();
    if (current && (current != m_owner))
    {
        MXB_ALERT("dcb_final_close(%p) called by %d, owned by %d.",
                  this,
                  current->id(),
                  m_owner->id());
        mxb_assert(m_owner == current);
    }
#endif
    mxb_assert(!m_open);

    if (is_polling())
    {
        disable_events();
    }

    shutdown();

    if (m_fd != FD_CLOSED)
    {
        // TODO: How could we get this far with a dcb->m_fd <= 0?

        if (::close(m_fd) < 0)
        {
            int eno = errno;
            errno = 0;
            MXB_ERROR("Failed to close socket %d on dcb %p: %d, %s",
                      m_fd,
                      this,
                      eno,
                      mxb_strerror(eno));
        }
        else
        {
            MXB_DEBUG("Closed socket %d on dcb %p.", m_fd, this);
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
 */
void DCB::socket_write_SSL()
{
    bool keep_writing = true;
    size_t total_written = 0;

    while (keep_writing && !m_writeq.empty())
    {
        // OpenSSL thread-local error queue should be cleared before an I/O op.
        ERR_clear_error();

        int writable = 0;
        if (m_encryption.retry_write_size > 0)
        {
            // Previous write failed, try again with the same data. According to testing,
            // this is not necessary. OpenSSL documentation claims that it is, so best to obey.
            writable = m_encryption.retry_write_size;
        }
        else
        {
            // SSL_write takes the number of bytes as int. It's imaginable that the writeq length
            // could be greater than INT_MAX, so limit the write amount.
            writable = std::min(m_writeq.length(), (size_t)INT_MAX);
        }

        // TODO: Use SSL_write_ex when can assume a more recent OpenSSL (Centos7 limitation).
        int res = SSL_write(m_encryption.handle, m_writeq.data(), writable);
        if (res > 0)
        {
            m_writeq.consume(res);
            total_written += res;

            m_encryption.retry_write_size = 0;
            m_encryption.write_want_read = false;
        }
        else
        {
            keep_writing = false;
            switch (SSL_get_error(m_encryption.handle, res))
            {
            case SSL_ERROR_ZERO_RETURN:
                // SSL-connection closed.
                trigger_hangup_event();
                break;

            case SSL_ERROR_WANT_READ:
                // Write-operation needs to read data that is not yet available. Go back to poll, wait for a
                // read-event, and then try to write again.
                m_encryption.write_want_read = true;
                m_encryption.retry_write_size = writable;
                break;

            case SSL_ERROR_WANT_WRITE:
                // Write buffer is full, go back to poll and wait. Equivalent to EWOULDBLOCK.
                m_encryption.write_want_read = false;
                m_encryption.retry_write_size = writable;
                break;

            default:
                // Report errors and shutdown the connection.
                if (log_errors_SSL(res) < 0)
                {
                    trigger_hangup_event();
                }
                break;
            }
        }
    }

    if (total_written > 0)
    {
        MXB_DEBUG("Wrote %lu bytes to dcb %p (%s) in state %s fd %d.",
                  total_written, this, whoami().c_str(), mxs::to_string(m_state), m_fd);

        m_last_write = mxs_clock();
    }
}

/**
 * Write data to the underlying socket. The data is taken from the DCB's write queue.
 */
void DCB::socket_write()
{
    mxb_assert(m_fd != FD_CLOSED);
    bool keep_writing = true;
    size_t total_written = 0;

    // Write until socket blocks, we run out of bytes or an error occurs.
    while (keep_writing && !m_writeq.empty())
    {
        auto writable_bytes = m_writeq.length();
        auto res = ::write(m_fd, m_writeq.data(), writable_bytes);
        if (res > 0)
        {
            MXB_DEBUG("%s\n%s", whoami().c_str(), mxb::hexdump(m_writeq.data(), res).c_str());
            m_writeq.consume(res);
            total_written += res;

            // Either writeq is consumed or socket could not accept all data. In either case,
            // stop writing.
            mxb_assert(m_writeq.empty() || (res < (int64_t)writable_bytes));
            keep_writing = false;
        }
        else if (res == 0)
        {
            mxb_assert(!true);
            keep_writing = false;
        }
        else
        {
            int eno = errno;
            // Is EPIPE a normal error?
            if (eno == EAGAIN || eno == EWOULDBLOCK || eno == EPIPE)
            {
                // Done for now, no more can be written.
                keep_writing = false;
            }
            else if (eno == EINTR)
            {
                // Just try again.
            }
            else
            {
                // Unexpected error.
                if (!m_silence_errors)
                {
                    MXB_ERROR("Write to %s %s in state %s failed: %d, %s",
                              mxs::to_string(m_role), m_remote.c_str(), mxs::to_string(m_state),
                              eno, mxb_strerror(eno));
                }

                keep_writing = false;
            }
        }
    }

    if (total_written > 0)
    {
        MXB_DEBUG("Wrote %lu bytes to dcb %p (%s) in state %s fd %d.",
                  total_written, this, whoami().c_str(), mxs::to_string(m_state), m_fd);

        m_last_write = mxs_clock();
    }
}

std::tuple<uint8_t*, size_t> DCB::calc_read_limit_strict(size_t maxbytes)
{
    // Should only be called when have a valid read limit and the limit is not yet reached.
    mxb_assert(maxbytes > m_readq.length());
    // Strict read limit, cannot read more than allowed from socket. Assume that maxbytes is
    // reasonable.
    auto max_read_limit = maxbytes - m_readq.length();
    auto [ptr, _1] = m_readq.prepare_to_write(max_read_limit);
    return {ptr, max_read_limit};
}

bool DCB::add_callback(Reason reason,
                       int (* callback)(DCB*, Reason, void*),
                       void* userdata)
{
    CALLBACK* cb;
    CALLBACK* ptr;
    CALLBACK* lastcb = NULL;

    if ((ptr = (CALLBACK*)MXB_MALLOC(sizeof(CALLBACK))) == NULL)
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
            MXB_FREE(ptr);
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

void DCB::remove_callbacks()
{
    while (m_callbacks)
    {
        CALLBACK* cb = m_callbacks;
        m_callbacks = m_callbacks->next;
        MXB_FREE(cb);
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
        MXB_ERROR("Failed to initialize SSL for connection.");
        return false;
    }

    if (SSL_set_fd(m_encryption.handle, m_fd) == 0)
    {
        MXB_ERROR("Failed to set file descriptor for SSL connection.");
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
                MXB_ERROR("Peer host '%s' does not match certificate: %s", r.c_str(), buf);
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
        MXB_ERROR("Failed to set socket options: %d, %s",
                  errno,
                  mxb_strerror(errno));
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
    {
    }

    void execute(Worker& worker) override final
    {
        RoutingWorker& rworker = static_cast<RoutingWorker&>(worker);
        const auto& dcbs = rworker.dcbs();

        for (auto it = dcbs.begin(); it != dcbs.end() && m_more; ++it)
        {
            DCB* dcb = *it;

            if (dcb->session())
            {
                if (!m_func(dcb, m_data))
                {
                    m_more = false;
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

    std::atomic_bool m_more {true};
};

uint32_t DCB::process_events(uint32_t events)
{
    mxb_assert(m_owner == RoutingWorker::get_current());

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
        /** SSL authentication is still going on, we need to call DCB::ssl_handshake
         * until it return 1 for success or -1 for error */
        if (m_encryption.state == SSLState::HANDSHAKE_REQUIRED)
        {
            return_code = ssl_handshake();
        }
        if (1 == return_code)
        {
            m_incomplete_read = false;
            m_read_amount = 0;
            m_handler->ready_for_reading(this);

            if (m_incomplete_read)
            {
                // If 'max_read_amount' has been specified, there may be a fake EPOLLIN event
                // that now must be removed.
                m_triggered_event &= ~EPOLLIN;

                rc |= mxb::poll_action::INCOMPLETE_READ;
                m_incomplete_read = false;
            }
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

uint32_t DCB::event_handler(uint32_t events)
{
    this_thread.current_dcb = this;
    uint32_t rv = process_events(events);

    // When all I/O events have been handled, we will immediately
    // process an added fake event. As the handling of a fake event
    // may lead to the addition of another fake event we loop until
    // there is no fake event or the dcb has been closed.

    while ((m_open) && (m_triggered_event != 0))
    {
        events = m_triggered_event;
        m_triggered_event = 0;

        m_is_fake_event = true;
        rv |= process_events(events);
        m_is_fake_event = false;
    }

    this_thread.current_dcb = nullptr;

    return rv;
}

int DCB::poll_fd() const
{
    return m_fd;
}

uint32_t DCB::handle_poll_events(mxb::Worker* worker, uint32_t events, Pollable::Context context)
{
    mxb_assert(worker == m_owner);

    uint32_t rval = 0;

    /**
     * Fake hangup events (e.g. from monitors) can cause a DCB to be closed
     * before the real events are processed. This makes it look like a closed
     * DCB is receiving events when in reality the events were received at the
     * same time the DCB was closed. If a closed DCB receives events they should
     * be ignored.
     *
     * @see FakeEventTask()
     */
    if (m_open)
    {
        rval = event_handler(events);
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
            mxb_assert(m_dcb->m_owner == RoutingWorker::get_current());
            m_dcb->m_is_fake_event = true;
            m_dcb->handle_poll_events(m_dcb->m_owner, m_ev, Pollable::NEW_CALL);
            m_dcb->m_is_fake_event = false;
        }
    }

private:
    DCB*     m_dcb;
    uint32_t m_ev;
    uint64_t m_uid;     /**< DCB UID guarantees we deliver the event to the correct DCB */
};

void DCB::add_event_via_loop(uint32_t ev)
{
    FakeEventTask* task = new(std::nothrow) FakeEventTask(this, ev);

    if (task)
    {
        m_owner->execute(std::unique_ptr<FakeEventTask>(task), Worker::EXECUTE_QUEUED);
    }
    else
    {
        MXB_OOM();
    }
}

void DCB::add_event(uint32_t ev)
{
    if (this == this_thread.current_dcb)
    {
        mxb_assert(m_owner == RoutingWorker::get_current());
        // If the fake event is added to the current DCB, we arrange for
        // it to be handled immediately in DCB::event_handler() when the handling
        // of the current events are done...

        m_triggered_event = ev;
    }
    else
    {
        // ... otherwise we post the fake event using the messaging mechanism.
        add_event_via_loop(ev);
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

bool DCB::set_reads_enabled(bool enable)
{
    uint32_t mask = THIS_UNIT::poll_events;

    if (enable)
    {
        // Restore triggered EPOLLIN events. The code that enables or disables events relies on epoll
        // notifications to trigger any suspended events so the same approach is taken here.
        m_triggered_event |= m_triggered_event_old & EPOLLIN;
        m_triggered_event_old &= ~EPOLLIN;
    }
    else
    {
        mask &= ~EPOLLIN;

        // Store the EPOLLIN event if one exists. This does not prevent new EPOLLIN events from being
        // triggered on this DCB but it does prevent an existing EPOLLIN event from causing a new read. Any
        // code that can potentially trigger writeq throttling (i.e. calls to routeQuery or clientReply) must
        // trigger events before the function call. Otherwise the writeq throttling is not effective as the
        // fake events get delivered regardless of the events that the DCB listens to.
        m_triggered_event_old |= m_triggered_event & EPOLLIN;
        m_triggered_event &= ~EPOLLIN;
    }

    mxb_assert(m_state == State::POLLING);
    mxb_assert(m_fd != FD_CLOSED);

    bool rv = false;
    RoutingWorker* worker = static_cast<RoutingWorker*>(this->owner());
    mxb_assert(worker == RoutingWorker::get_current());

    return worker->modify_pollable(mask, this);
}

bool DCB::enable_events()
{
    mxb_assert_message(m_state == State::CREATED || m_state == State::NOPOLLING,
                       "State is: %s", mxs::to_string(m_state));

    bool rv = false;
    mxb_assert(m_owner == RoutingWorker::get_current());

    if (m_owner->add_pollable(THIS_UNIT::poll_events, this))
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
    mxb_assert_message(m_state == State::POLLING,
                       "State is: %s", mxs::to_string(m_state));
    mxb_assert(m_fd != FD_CLOSED);

    bool rv = true;
    mxb_assert(m_owner == RoutingWorker::get_current());

    // We unconditionally set the state, even if the actual removal might fail.
    m_state = State::NOPOLLING;

    // When BLR creates an internal DCB, it will set its state to
    // State::NOPOLLING and the fd will be FD_CLOSED.
    if (m_fd != FD_CLOSED)
    {
        // Remove any manually added read events, then remove fd from epoll.
        m_triggered_event_old = m_triggered_event;
        m_triggered_event = 0;
        if (!m_owner->remove_pollable(this))
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
    mxb_assert(dcb->role() == DCB::Role::BACKEND);
    auto* be_dcb = static_cast<BackendDCB*>(dcb);
    auto* session = static_cast<Session*>(be_dcb->session());
    auto* client_dcb = session->client_connection()->dcb();

    if (client_dcb->is_polling())
    {
        if (reason == DCB::Reason::HIGH_WATER)
        {
            client_dcb->set_reads_enabled(false);
            MXB_INFO("Write buffer size of connection to server %s reached %s. Pausing reading from "
                     "client %s until buffer size falls to %s.", be_dcb->server()->name(),
                     CN_WRITEQ_HIGH_WATER, session->user_and_host().c_str(), CN_WRITEQ_LOW_WATER);
        }
        else if (reason == DCB::Reason::LOW_WATER)
        {
            if (client_dcb->set_reads_enabled(true))
            {
                MXB_INFO("Write buffer size of connection to server %s fell to %s. Resuming reading "
                         "from client %s.", be_dcb->server()->name(), CN_WRITEQ_LOW_WATER,
                         session->user_and_host().c_str());
            }
            else
            {
                MXB_ERROR("Could not re-enable I/O events for connection to client %s. Closing session.",
                          session->user_and_host().c_str());
                client_dcb->trigger_hangup_event();
            }
        }
    }

    return 0;
}

/**
 * Called by client dcb when its writeq reaches high or low water mark. Pauses or resumes
 * backend connections.
 *
 * @param dcb      client dcb
 * @param reason   Why the callback was called
 * @param userdata Unused
 * @return Always 0
 */
static int downstream_throttle_callback(DCB* dcb, DCB::Reason reason, void* userdata)
{
    mxb_assert(dcb->role() == DCB::Role::CLIENT);
    auto* session = static_cast<Session*>(dcb->session());
    auto& be_conns = session->backend_connections();

    if (reason == DCB::Reason::HIGH_WATER)
    {
        // Disable events for backend dcbs of the session.
        int dcbs_paused = 0;
        for (auto& be_conn : be_conns)
        {
            auto* be_dcb = be_conn->dcb();
            if (be_dcb->is_polling())
            {
                if (be_dcb->set_reads_enabled(false))
                {
                    dcbs_paused++;
                }
            }
        }

        if (dcbs_paused > 0)
        {
            MXB_INFO("Write buffer size of connection to client %s reached %s. Pausing reading from %i "
                     "backend connections until buffer size falls to %s.", session->user_and_host().c_str(),
                     CN_WRITEQ_HIGH_WATER, dcbs_paused, CN_WRITEQ_LOW_WATER);
        }
    }
    else if (reason == DCB::Reason::LOW_WATER)
    {
        // Enable events for backend dcbs of the session.
        int dcbs_resumed = 0;
        for (auto& be_conn : be_conns)
        {
            auto* be_dcb = be_conn->dcb();
            if (be_dcb->is_polling())
            {
                if (be_dcb->set_reads_enabled(true))
                {
                    dcbs_resumed++;
                }
                else
                {
                    MXB_ERROR("Could not re-enable I/O events for connection to server %s. Closing "
                              "session of %s.", be_dcb->server()->name(), session->user_and_host().c_str());
                    dcb->trigger_hangup_event();
                }
            }
        }

        if (dcbs_resumed > 0)
        {
            MXB_INFO("Write buffer size of connection to client %s fell to %s. Resuming reading from %i "
                     "backend connections.", session->user_and_host().c_str(), CN_WRITEQ_LOW_WATER,
                     dcbs_resumed);
        }
    }

    return 0;
}

SERVICE* DCB::service() const
{
    return m_session->service;
}

mxb::Json DCB::get_memory_statistics() const
{
    mxb::Json rv;

    size_t total = 0;

    auto writeq = m_writeq.capacity();
    total += writeq;
    rv.set_int("writeq", writeq);

    auto readq = m_readq.capacity();
    total += readq;
    rv.set_int("readq", readq);

    auto misc = runtime_size() - total;
    total += misc;
    rv.set_int("misc", misc);

    rv.set_int("total", total);

    return rv;
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

std::string ClientDCB::whoami() const
{
    return m_session->user_and_host();
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
    if (m_high_water)
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

    set_SSL_mode_bits(m_encryption.handle);
    int ssl_rval = SSL_accept(m_encryption.handle);

    switch (SSL_get_error(m_encryption.handle, ssl_rval))
    {
    case SSL_ERROR_NONE:
        MXB_DEBUG("SSL_accept done for %s", m_remote.c_str());
        m_encryption.state = SSLState::ESTABLISHED;
        m_encryption.read_want_write = false;
        return verify_peer_host() ? 1 : -1;

    case SSL_ERROR_WANT_READ:
        MXB_DEBUG("SSL_accept ongoing want read for %s", m_remote.c_str());
        return 0;

    case SSL_ERROR_WANT_WRITE:
        MXB_DEBUG("SSL_accept ongoing want write for %s", m_remote.c_str());
        m_encryption.read_want_write = true;
        return 0;

    case SSL_ERROR_ZERO_RETURN:
        MXB_DEBUG("SSL error, shut down cleanly during SSL accept %s", m_remote.c_str());
        log_errors_SSL(0);
        trigger_hangup_event();
        return 0;

    case SSL_ERROR_SYSCALL:
        MXB_DEBUG("SSL connection SSL_ERROR_SYSCALL error during accept %s", m_remote.c_str());
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
        MXB_DEBUG("SSL connection shut down with error during SSL accept %s", m_remote.c_str());
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

void ClientDCB::set_remote_ip_port(const sockaddr_storage& ip, std::string&& ip_str)
{
    m_ip = ip;
    set_remote(std::move(ip_str));
}

void DCB::close(DCB* dcb)
{
#if defined (SS_DEBUG)
    mxb_assert(dcb->m_state != State::DISCONNECTED && dcb->m_fd != FD_CLOSED && dcb->m_manager);
    auto* current = RoutingWorker::get_current();
    mxb_assert(current && current == dcb->m_owner);
#endif

    if (dcb->m_open)
    {
        dcb->m_open = false;
        dcb->m_manager->destroy(dcb);
    }
    else
    {
        // TODO: Will this happen on a regular basis?
        MXB_WARNING("DCB::close(%p) called on a closed dcb.", dcb);
        mxb_assert(!true);
    }
}

size_t DCB::readq_peek(size_t n_bytes, uint8_t* dst) const
{
    return m_readq.copy_data(0, n_bytes, dst);
}

void DCB::unread(GWBUF* buffer)
{
    if (buffer)
    {
        m_readq.merge_front(move(*buffer));
        delete buffer;
    }
}

void DCB::unread(GWBUF&& buffer)
{
    m_readq.merge_front(move(buffer));
}

void DCB::set_remote(string&& remote)
{
    m_remote = std::move(remote);
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

    if (m_high_water)
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
                    backend_dcb->m_is_fake_event = false;
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

    set_SSL_mode_bits(m_encryption.handle);
    m_encryption.state = SSLState::HANDSHAKE_REQUIRED;
    ssl_rval = SSL_connect(m_encryption.handle);

    switch (SSL_get_error(m_encryption.handle, ssl_rval))
    {
    case SSL_ERROR_NONE:
        MXB_DEBUG("SSL_connect done for %s", m_remote.c_str());
        m_encryption.state = SSLState::ESTABLISHED;
        m_encryption.read_want_write = false;
        return_code = verify_peer_host() ? 1 : -1;
        break;

    case SSL_ERROR_WANT_READ:
        MXB_DEBUG("SSL_connect ongoing want read for %s", m_remote.c_str());
        return_code = 0;
        break;

    case SSL_ERROR_WANT_WRITE:
        MXB_DEBUG("SSL_connect ongoing want write for %s", m_remote.c_str());
        m_encryption.read_want_write = true;
        return_code = 0;
        break;

    case SSL_ERROR_ZERO_RETURN:
        MXB_DEBUG("SSL error, shut down cleanly during SSL connect %s", m_remote.c_str());
        if (log_errors_SSL(0) < 0)
        {
            trigger_hangup_event();
        }
        return_code = 0;
        break;

    case SSL_ERROR_SYSCALL:
        MXB_DEBUG("SSL connection shut down with SSL_ERROR_SYSCALL during SSL connect %s", m_remote.c_str());
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
        MXB_DEBUG("SSL connection shut down with error during SSL connect %s", m_remote.c_str());
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

    if (m_high_water)
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

std::string BackendDCB::whoami() const
{
    return m_server->name();
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
    mxb_assert(mxs::MainWorker::is_current());
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

void dcb_set_current(DCB* dcb)
{
    this_thread.current_dcb = dcb;
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

size_t mxs::ClientConnectionBase::sizeof_buffers() const
{
    return m_dcb ? m_dcb->runtime_size() : 0;
}
