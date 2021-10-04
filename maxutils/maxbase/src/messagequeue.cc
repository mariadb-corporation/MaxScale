/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-09-20
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxbase/messagequeue.hh>
#include <cerrno>
#include <cstring>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <maxbase/assert.h>
#include <maxbase/log.hh>
#include <maxbase/string.hh>
#include <maxbase/worker.hh>

namespace
{
using Guard = std::lock_guard<std::mutex>;
using std::move;
}

namespace maxbase
{

MessageQueue::MessageQueue(Handler* pHandler, int event_fd)
    : mxb::PollData(&MessageQueue::poll_handler)
    , m_handler(*pHandler)
    , m_event_fd(event_fd)
{
    mxb_assert(pHandler);
}

MessageQueue::~MessageQueue()
{
    if (m_pWorker)
    {
        m_pWorker->remove_fd(m_event_fd);
    }
    close(m_event_fd);
}

// static
MessageQueue* MessageQueue::create(Handler* pHandler)
{
    MessageQueue* pThis = nullptr;
    int event_fd = eventfd(0, EFD_NONBLOCK);
    if (event_fd >= 0)
    {
        pThis = new MessageQueue(pHandler, event_fd);
    }
    else
    {
        int eno = errno;
        MXB_ERROR("Could not create eventfd for message queue. Error %d: %s", eno, mxb_strerror(eno));
    }
    return pThis;
}

bool MessageQueue::post(const Message& message)
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

bool MessageQueue::add_to_worker(Worker* pWorker)
{
    if (m_pWorker)
    {
        m_pWorker->remove_fd(m_event_fd);
        m_pWorker = nullptr;
    }

    if (pWorker->add_fd(m_event_fd, EPOLLIN, this))
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
        m_pWorker->remove_fd(m_event_fd);
        m_pWorker = NULL;
    }

    return pWorker;
}

uint32_t MessageQueue::handle_poll_events(Worker* pWorker, uint32_t events)
{
    // Only the owning worker's thread should run this function.
    mxb_assert(Worker::get_current() == m_pWorker);
    // We only expect EPOLLIN events since that was subscribed to.
    mxb_assert(events == EPOLLIN);
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
            mxb_assert(n_msgs < 1000);      // Should not see more in tests, adjust if needed.
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

// static
uint32_t MessageQueue::poll_handler(POLL_DATA* pData, WORKER* pWorker, uint32_t events)
{
    MessageQueue* pThis = static_cast<MessageQueue*>(pData);

    return pThis->handle_poll_events(static_cast<Worker*>(pWorker), events);
}

void MessageQueue::swap_messages_and_work()
{
    Guard guard(m_messages_lock);
    m_work.swap(m_messages);
}

void MessageQueue::add_message(const MessageQueue::Message& message)
{
    Guard guard(m_messages_lock);
    m_messages.emplace_back(message);
}
}
