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

#include <maxscale/messagequeue.hh>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <fstream>
#include <maxscale/debug.h>
#include <maxscale/log.h>
#include "internal/routingworker.hh"

namespace
{

static struct
{
    bool initialized;
    int  pipe_max_size;
} this_unit =
{
    false
};

int get_pipe_max_size()
{
    int size = 65536; // Default value from pipe(7)
    std::ifstream file("/proc/sys/fs/pipe-max-size");

    if (file.good())
    {
        file >> size;
    }

    return size;
}

}

namespace maxscale
{

MessageQueue::MessageQueue(Handler* pHandler, int read_fd, int write_fd)
    : mxb::PollData(&MessageQueue::poll_handler)
    , m_handler(*pHandler)
    , m_read_fd(read_fd)
    , m_write_fd(write_fd)
    , m_pWorker(NULL)
{
    ss_dassert(pHandler);
    ss_dassert(read_fd);
    ss_dassert(write_fd);
}

MessageQueue::~MessageQueue()
{
    if (m_pWorker)
    {
        m_pWorker->remove_fd(m_read_fd);
    }

    close(m_read_fd);
    close(m_write_fd);
}

//static
bool MessageQueue::init()
{
    ss_dassert(!this_unit.initialized);

    this_unit.initialized = true;
    this_unit.pipe_max_size = get_pipe_max_size();

    return this_unit.initialized;
}

//static
void MessageQueue::finish()
{
    ss_dassert(this_unit.initialized);
    this_unit.initialized = false;
}

//static
MessageQueue* MessageQueue::create(Handler* pHandler)
{
    ss_dassert(this_unit.initialized);

    /* From "man 7 pipe"
     * ----
     *
     * O_NONBLOCK enabled, n <= PIPE_BUF
     *   If  there  is room to write n bytes to the pipe, then write(2)
     *   succeeds immediately, writing all n bytes; otherwise write(2)
     *   fails, with errno set to EAGAIN.
     *
     * ... (On Linux, PIPE_BUF is 4096 bytes.)
     *
     * ----
     *
     * As O_NONBLOCK is set and the messages are less than 4096 bytes,
     * O_DIRECT should not be needed and we should be safe without it.
     *
     * However, to be in the safe side, we first try whether it is supported,
     * and if not, we create the pipe without O_DIRECT.
     */

    MessageQueue* pThis = NULL;

    int fds[2];

    int rv = pipe2(fds, O_NONBLOCK | O_CLOEXEC | O_DIRECT);

    if ((rv != 0) && (errno == EINVAL))
    {
        // Ok, apparently the kernel does not support O_DIRECT. Let's try without.
        rv = pipe2(fds, O_NONBLOCK | O_CLOEXEC);

        if (rv == 0)
        {
            // Succeeded, so apparently it was the missing support for O_DIRECT.
            MXS_WARNING("Platform does not support O_DIRECT in conjunction with pipes, "
                        "using without.");
        }
    }

    if (rv == 0)
    {
        int read_fd = fds[0];
        int write_fd = fds[1];
#ifdef F_SETPIPE_SZ
        /**
         * Increase the pipe buffer size on systems that support it. Modifying
         * the buffer size of one fd will also increase it for the other.
         */
        if (fcntl(fds[0], F_SETPIPE_SZ, this_unit.pipe_max_size) == -1)
        {
            MXS_WARNING("Failed to increase pipe buffer size to '%d': %d, %s",
                        this_unit.pipe_max_size, errno, mxs_strerror(errno));
        }
#endif
        pThis = new (std::nothrow) MessageQueue(pHandler, read_fd, write_fd);

        if (!pThis)
        {
            MXS_OOM();
            close(read_fd);
            close(write_fd);
        }
    }
    else
    {
        MXS_ERROR("Could not create pipe for worker: %s", mxs_strerror(errno));
    }

    return pThis;
}

bool MessageQueue::post(const Message& message) const
{
    // NOTE: No logging here, this function must be signal safe.
    bool rv = false;

    ss_dassert(m_pWorker);
    if (m_pWorker)
    {
        /**
         * This is a stopgap measure to solve MXS-1983 that causes Resource temporarily
         * unavailable errors. The errors are caused by the pipe buffer being too small to
         * hold all worker messages. By retrying a limited number of times before giving
         * up, the success rate for posted messages under heavy load increases
         * significantly.
         */
        int fast = 0;
        int slow = 0;
        const int fast_size = 100;
        const int slow_limit = 3;
        ssize_t n;

        while (true)
        {
            n = write(m_write_fd, &message, sizeof(message));
            rv = (n == sizeof(message));

            if (n == -1 && (errno == EAGAIN || errno == EWOULDBLOCK))
            {
                if (++fast > fast_size)
                {
                    fast = 0;

                    if (++slow >= slow_limit)
                    {
                        break;
                    }
                    else
                    {
                        sched_yield();
                    }
                }
            }
            else
            {
                break;
            }
        }

        if (n == -1)
        {
            MXS_ERROR("Failed to write message: %d, %s", errno, mxs_strerror(errno));

            static bool warn_pipe_buffer_size = true;

            if ((errno == EAGAIN || errno == EWOULDBLOCK) && warn_pipe_buffer_size)
            {
                MXS_ERROR("Consider increasing pipe buffer size (sysctl fs.pipe-max-size)");
                warn_pipe_buffer_size = false;
            }
        }
    }
    else
    {
        MXS_ERROR("Attempt to post using a message queue that is not added to a worker.");
    }

    return rv;
}

bool MessageQueue::add_to_worker(Worker* pWorker)
{
    if (m_pWorker)
    {
        m_pWorker->remove_fd(m_read_fd);
        m_pWorker = NULL;
    }

    if (pWorker->add_fd(m_read_fd, EPOLLIN, this))
    {
        m_pWorker = pWorker;
    }

    return m_pWorker != NULL;
}

Worker* MessageQueue::remove_from_worker()
{
    Worker* pWorker = m_pWorker;

    if (m_pWorker)
    {
        m_pWorker->remove_fd(m_read_fd);
        m_pWorker = NULL;
    }

    return pWorker;
}

uint32_t MessageQueue::handle_poll_events(Worker* pWorker, uint32_t events)
{
    uint32_t rc = MXB_POLL_NOP;

    ss_dassert(pWorker == m_pWorker);

    // We only expect EPOLLIN events.
    ss_dassert(((events & EPOLLIN) != 0) && ((events & ~EPOLLIN) == 0));

    if (events & EPOLLIN)
    {
        Message message;

        ssize_t n;

        do
        {
            n = read(m_read_fd, &message, sizeof(message));

            if (n == sizeof(message))
            {
                m_handler.handle_message(*this, message);
            }
            else if (n == -1)
            {
                if (errno != EWOULDBLOCK)
                {
                    MXS_ERROR("Worker could not read from pipe: %s", mxs_strerror(errno));
                }
            }
            else if (n != 0)
            {
                // This really should not happen as the pipe is in message mode. We
                // should either get a message, nothing at all or an error. In non-debug
                // mode we continue reading in order to empty the pipe as otherwise the
                // thread may hang.
                MXS_ERROR("MessageQueue could only read %ld bytes from pipe, although "
                          "expected %lu bytes.", n, sizeof(message));
                ss_dassert(!true);
            }
        }
        while ((n != 0) && (n != -1));

        rc = MXB_POLL_READ;
    }

    return rc;
}

//static
uint32_t MessageQueue::poll_handler(MXB_POLL_DATA* pData, MXB_WORKER* pWorker, uint32_t events)
{
    MessageQueue* pThis = static_cast<MessageQueue*>(pData);

    return pThis->handle_poll_events(static_cast<Worker*>(pWorker), events);
}

}
