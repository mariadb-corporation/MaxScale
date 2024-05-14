/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-04-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxbase/messagequeue.hh>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <vector>
#include <fcntl.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <maxbase/assert.hh>
#include <maxbase/log.hh>
#include <maxbase/pretty_print.hh>
#include <maxbase/string.hh>
#include <maxbase/worker.hh>

namespace
{
using Guard = std::lock_guard<std::mutex>;
using std::move;

const char* PIPE_FULL_WARNING =
    " Consider increasing the pipe buffer size (sysctl fs.pipe-max-size). Slow domain name servers "
    "can also cause problems. To disable reverse name resolution, add 'skip_name_resolve=true' under "
    "the '[maxscale]' section.";

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
    int size = 65536;   // Default value from pipe(7)
    std::ifstream file("/proc/sys/fs/pipe-max-size");

    if (file.good())
    {
        file >> size;
    }

    return size;
}
}

namespace maxbase
{

//
// MessageQueue
//
MessageQueue::MessageQueue()
{
}

MessageQueue::~MessageQueue()
{
}

MessageQueue* MessageQueue::create(Kind kind, Handler* pHandler)
{
    MessageQueue* pMq = nullptr;

    switch (kind)
    {
    case EVENT:
        pMq = EventMessageQueue::create(pHandler);
        break;

    case PIPE:
        pMq = PipeMessageQueue::create(pHandler);
        break;

    default:
        mxb_assert(!true);
    }

    return pMq;
}

//
// EventMessageQueue
//
EventMessageQueue::EventMessageQueue(Handler* pHandler, int event_fd)
    : m_handler(*pHandler)
    , m_event_fd(event_fd)
{
    mxb_assert(pHandler);
}

EventMessageQueue::~EventMessageQueue()
{
    if (m_pWorker)
    {
        m_pWorker->remove_pollable(this);
    }
    close(m_event_fd);
}

// static
EventMessageQueue* EventMessageQueue::create(Handler* pHandler)
{
    EventMessageQueue* pThis = nullptr;
    int event_fd = eventfd(0, EFD_NONBLOCK);
    if (event_fd >= 0)
    {
        pThis = new EventMessageQueue(pHandler, event_fd);
    }
    else
    {
        int eno = errno;
        MXB_ERROR("Could not create eventfd for message queue. Error %d: %s", eno, mxb_strerror(eno));
    }
    return pThis;
}

bool EventMessageQueue::post(const Message& message)
{
    bool rv = false;
    mxb_assert(m_pWorker);
    if (m_pWorker)
    {
        add_message(message);

        // Signal eventfd that a message is available.
        if (eventfd_write(m_event_fd, 1) != 0)
        {
            // This can only happen in (very) unlikely situations.
            int eno = errno;
            MXB_ERROR("Failed to write to eventfd of worker %d. Error %d: %s",
                      m_pWorker->id(), eno, mxb_strerror(eno));
            mxb_assert(!true);
        }
        rv = true;
    }
    else
    {
        MXB_ERROR("Attempt to post using a message queue that is not added to a worker.");
    }

    return rv;
}

bool EventMessageQueue::add_to_worker(Worker* pWorker)
{
    if (m_pWorker)
    {
        m_pWorker->remove_pollable(this);
        m_pWorker = nullptr;
    }

    if (pWorker->add_pollable(EPOLLIN, this))
    {
        m_pWorker = pWorker;
    }

    return m_pWorker != NULL;
}

Worker* EventMessageQueue::remove_from_worker()
{
    Worker* pWorker = m_pWorker;

    if (m_pWorker)
    {
        m_pWorker->remove_pollable(this);
        m_pWorker = NULL;
    }

    return pWorker;
}

uint32_t EventMessageQueue::handle_poll_events(Worker* pWorker, uint32_t events, Pollable::Context)
{
    mxb_assert(pWorker == m_pWorker);
    mxb_assert(((events & EPOLLIN) != 0) && ((events & ~EPOLLIN) == 0));

    uint32_t rc = poll_action::NOP;

    if (events & EPOLLIN)
    {
        MXB_AT_DEBUG(m_total_events++);
        eventfd_t event_count {0};
        if (eventfd_read(m_event_fd, &event_count) == 0)
        {
            swap_messages_and_work();

#ifdef SS_DEBUG
            // The event count may not match the number of messages in the container since the
            // former is modified outside the mutex.
            size_t n_msgs = m_work.size();
            if (n_msgs == 1)
            {
                m_single_msg_events++;
            }
            else if (n_msgs > 1)
            {
                m_multi_msg_events++;
            }
            m_max_msgs_seen = std::max(m_max_msgs_seen, n_msgs);
            m_total_msgs += n_msgs;
            m_ave_msgs_per_event = (double)m_total_msgs / m_total_events;
#endif

            for (auto& msg : m_work)
            {
                m_handler.handle_message(*this, msg);
            }
            m_work.clear();
            rc = poll_action::READ;
        }
        else
        {
            int eno = errno;
            // Even EAGAIN can be considered an error, as we listen for EPOLLIN.
            MXB_ERROR("Failed to read from eventfd of worker %d. Error %d: %s",
                      m_pWorker->id(), eno, mxb_strerror(eno));
            rc = poll_action::ERROR;
        }
    }

    return rc;
}

void EventMessageQueue::swap_messages_and_work()
{
    Guard guard(m_messages_lock);
    m_work.swap(m_messages);
}

void EventMessageQueue::add_message(const MessageQueue::Message& message)
{
    Guard guard(m_messages_lock);
    m_messages.emplace_back(message);
    mxb_assert(m_messages.size() < 40000);      // Should not see more in tests, adjust if needed.
}

//
// PipeMessageQueue
//
PipeMessageQueue::PipeMessageQueue(Handler* pHandler, int read_fd, int write_fd)
    : m_handler(*pHandler)
    , m_read_fd(read_fd)
    , m_write_fd(write_fd)
    , m_pWorker(NULL)
{
    mxb_assert(pHandler);
    mxb_assert(read_fd);
    mxb_assert(write_fd);
}

PipeMessageQueue::~PipeMessageQueue()
{
    if (m_pWorker)
    {
        m_pWorker->remove_pollable(this);
    }

    close(m_read_fd);
    close(m_write_fd);
}

// static
bool PipeMessageQueue::init()
{
    mxb_assert(!this_unit.initialized);

    this_unit.initialized = true;
    this_unit.pipe_max_size = get_pipe_max_size();

    return this_unit.initialized;
}

// static
void PipeMessageQueue::finish()
{
    mxb_assert(this_unit.initialized);
    this_unit.initialized = false;
}

// static
PipeMessageQueue* PipeMessageQueue::create(Handler* pHandler)
{
    mxb_assert(this_unit.initialized);

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
     * O_DIRECT is not be needed and we are safe without it. Enabling
     * it appears to cause each write to take up the maximum of 4096
     * bytes which would dramatically reduce the amount of messages
     * that could be sent.
     */

    PipeMessageQueue* pThis = NULL;

    int fds[2];

    int rv = pipe2(fds, O_NONBLOCK | O_CLOEXEC);

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
            MXB_WARNING("Failed to increase pipe buffer size to '%d': %d, %s. "
                        "Increase pipe-user-pages-soft (sysctl fs.pipe-user-pages-soft) "
                        "or reduce pipe-max-size (sysctl fs.pipe-max-size).",
                        this_unit.pipe_max_size, errno, mxb_strerror(errno));
        }
        else
        {
            static int current_pipe_max_size = 0;
            static std::mutex pipe_size_lock;
            std::lock_guard<std::mutex> guard(pipe_size_lock);

            if (current_pipe_max_size == 0)
            {
                current_pipe_max_size = this_unit.pipe_max_size;
                MXB_NOTICE("Worker message queue size: %s",
                           mxb::pretty_size(this_unit.pipe_max_size).c_str());
            }
        }
#endif
        pThis = new(std::nothrow) PipeMessageQueue(pHandler, read_fd, write_fd);

        if (!pThis)
        {
            MXB_OOM();
            close(read_fd);
            close(write_fd);
        }
    }
    else
    {
        MXB_ERROR("Could not create pipe for worker: %s", mxb_strerror(errno));
    }

    return pThis;
}

bool PipeMessageQueue::post(const Message& message)
{
    // NOTE: No logging here, this function must be signal safe.
    bool rv = false;

    mxb_assert(m_pWorker);
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
        const int slow_limit = 5;
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

                    if (slow++ == slow_limit)
                    {
                        const char* msg = "";
                        static bool warn_when_pipe_full = true;

                        if (warn_when_pipe_full)
                        {
                            msg = PIPE_FULL_WARNING;
                            warn_when_pipe_full = false;
                        }

                        auto source_worker = mxb::Worker::get_current();
                        std::string source_id = source_worker ?
                            std::to_string(source_worker->id()) : "<no worker>";

                        MXB_WARNING("Worker %s attempted to send a message to worker %d but it has been "
                                    "busy for over %d milliseconds.%s",
                                    source_id.c_str(), m_pWorker->id(), slow_limit, msg);
                        break;
                    }

                    std::this_thread::sleep_for(1ms);
                }
                else
                {
                    sched_yield();
                }
            }
            else
            {
                break;
            }
        }

        if (n == -1)
        {
            MXB_ERROR("Failed to write message to worker %d: %d, %s",
                      m_pWorker->id(), errno, mxb_strerror(errno));
        }
    }
    else
    {
        MXB_ERROR("Attempt to post using a message queue that is not added to a worker.");
    }

    return rv;
}

bool PipeMessageQueue::add_to_worker(Worker* pWorker)
{
    if (m_pWorker)
    {
        m_pWorker->remove_pollable(this);
        m_pWorker = NULL;
    }

    if (pWorker->add_pollable(EPOLLIN | EPOLLET, this))
    {
        m_pWorker = pWorker;
    }

    return m_pWorker != NULL;
}

Worker* PipeMessageQueue::remove_from_worker()
{
    Worker* pWorker = m_pWorker;

    if (m_pWorker)
    {
        m_pWorker->remove_pollable(this);
        m_pWorker = NULL;
    }

    return pWorker;
}

uint32_t PipeMessageQueue::handle_poll_events(Worker* pWorker, uint32_t events, Pollable::Context)
{
    uint32_t rc = poll_action::NOP;

    mxb_assert(pWorker == m_pWorker);
    mxb_assert(((events & EPOLLIN) != 0) && ((events & ~EPOLLIN) == 0));

    if (events & EPOLLIN)
    {
        std::vector<Message> messages;
        ssize_t n;

        do
        {
            Message message;
            n = read(m_read_fd, &message, sizeof(message));

            if (n == sizeof(message))
            {
                messages.push_back(std::move(message));
            }
            else if (n == -1)
            {
                if (errno != EWOULDBLOCK)
                {
                    MXB_ERROR("Worker could not read from pipe: %s", mxb_strerror(errno));
                }
            }
            else if (n != 0)
            {
                // This really should not happen as the pipe is in message mode. We
                // should either get a message, nothing at all or an error. In non-debug
                // mode we continue reading in order to empty the pipe as otherwise the
                // thread may hang.
                MXB_ERROR("PipeMessageQueue could only read %ld bytes from pipe, although "
                          "expected %lu bytes.",
                          n,
                          sizeof(message));
                mxb_assert(!true);
            }
        }
        while ((n != 0) && (n != -1));

        rc = poll_action::READ;

        for (const auto& message : messages)
        {
            m_handler.handle_message(*this, message);
        }
    }

    return rc;
}
}
