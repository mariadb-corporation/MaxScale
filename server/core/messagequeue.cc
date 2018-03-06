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

#include "internal/messagequeue.hh"
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <sys/utsname.h>
#include <maxscale/debug.h>
#include <maxscale/log_manager.h>
#include "internal/worker.hh"

namespace
{

static struct
{
    bool initialized;
    int  pipe_flags;
} this_unit =
{
    false,
    O_NONBLOCK | O_CLOEXEC
};

}

namespace maxscale
{

MessageQueue::MessageQueue(Handler* pHandler, int read_fd, int write_fd)
    : MxsPollData(&MessageQueue::poll_handler)
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
     * However, to be in the safe side, if we run on kernel version >= 3.4
     * we use it.
     */

    utsname u;

    if (uname(&u) == 0)
    {
        char* p;
        char* zMajor = strtok_r(u.release, ".", &p);
        char* zMinor = strtok_r(NULL, ".", &p);

        if (zMajor && zMinor)
        {
            int major = atoi(zMajor);
            int minor = atoi(zMinor);

            if (major >= 3 && minor >= 4)
            {
                // O_DIRECT for pipes is supported from kernel 3.4 onwards.
                this_unit.pipe_flags |= O_DIRECT;
            }
            else
            {
                MXS_NOTICE("O_DIRECT is not supported for pipes on Linux kernel %s "
                           "(supported from version 3.4 onwards), NOT using it.",
                           u.release);
            }
        }
        else
        {
            MXS_WARNING("Syntax used in utsname.release seems to have changed, "
                        "not able to figure out current kernel version. Assuming "
                        "O_DIRECT is not supported for pipes.");
        }
    }
    else
    {
        MXS_WARNING("uname() failed, assuming O_DIRECT is not supported for pipes: %s",
                    mxs_strerror(errno));
    }

    this_unit.initialized = true;

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

    MessageQueue* pThis = NULL;

    int fds[2];
    if (pipe2(fds, this_unit.pipe_flags) == 0)
    {
        int read_fd = fds[0];
        int write_fd = fds[1];

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
        ssize_t n = write(m_write_fd, &message, sizeof(message));
        rv = (n == sizeof(message));

        if (n == -1)
        {
            MXS_ERROR("Failed to write message: %d, %s", errno, mxs_strerror(errno));
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

uint32_t MessageQueue::handle_poll_events(int thread_id, uint32_t events)
{
    uint32_t rc = MXS_POLL_NOP;

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

        rc = MXS_POLL_READ;
    }

    return rc;
}

//static
uint32_t MessageQueue::poll_handler(MXS_POLL_DATA* pData, int thread_id, uint32_t events)
{
    MessageQueue* pThis = static_cast<MessageQueue*>(pData);

    return pThis->handle_poll_events(thread_id, events);
}

}
