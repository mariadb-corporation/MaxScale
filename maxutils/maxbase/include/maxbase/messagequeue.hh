/*
 * Copyright (c) 2018 MariaDB Corporation Ab
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
 #pragma once

#include <maxbase/ccdefs.hh>
#include <maxbase/poll.hh>

namespace maxbase
{

class MaxBase;
class MessageQueue;
class Worker;

/**
 * An instance of @c MessageQueueMessage can be sent over a @c MessageQueue from
 * one context to another. The instance will be copied verbatim without any
 * interpretation, so if the same message is sent to multiple recipients it is
 * the caller's and recipient's responsibility to manage the lifetime and
 * concurrent access of anything possibly pointed to from the message.
 */
class MessageQueueMessage /* final */
{
public:
    /**
     * Constructor
     *
     * @param id    The id of the message. The meaning is an affair between the sender
     *              and the recipient.
     * @param arg1  First argument.
     * @param arg2  Second argument.
     */
    explicit MessageQueueMessage(uint64_t id = 0, intptr_t arg1 = 0, intptr_t arg2 = 0)
        : m_id(id)
        , m_arg1(arg1)
        , m_arg2(arg2)
    {
    }

    uint32_t id() const
    {
        return m_id;
    }

    intptr_t arg1() const
    {
        return m_arg1;
    }

    intptr_t arg2() const
    {
        return m_arg2;
    }

    MessageQueueMessage& set_id(uint64_t id)
    {
        m_id = id;
        return *this;
    }

    MessageQueueMessage& set_arg1(intptr_t arg1)
    {
        m_arg1 = arg1;
        return *this;
    }

    MessageQueueMessage& set_arg2(intptr_t arg2)
    {
        m_arg2 = arg2;
        return *this;
    }

private:
    uint64_t m_id;
    intptr_t m_arg1;
    intptr_t m_arg2;
};


/**
 * A @c MessageQueueHandler will be delivered messages received over a
 * @c MessageQueue.
 */
class MessageQueueHandler
{
public:
    /**
     * Message delivery.
     *
     * @param queue    The queue over which the message was received.
     * @param message  The message.
     */
    virtual void handle_message(MessageQueue& queue, const MessageQueueMessage& message) = 0;
};


/**
 * The class @c MessageQueue provides a cross thread message queue implemented
 * on top of a pipe.
 */
class MessageQueue : private mxb::PollData
{
    MessageQueue(const MessageQueue&);
    MessageQueue& operator = (const MessageQueue&);

public:
    typedef MessageQueueHandler Handler;
    typedef MessageQueueMessage Message;

    /**
     * Creates a @c MessageQueue with the provided handler.
     *
     * @param pHandler  The handler that will receive the messages sent over the
     *                  message queue. Note that the handler *must* remain valid
     *                  for the lifetime of the @c MessageQueue.
     *
     * @return A pointer to a new @c MessageQueue or NULL if an error occurred.
     *
     * @attention Before the message queue can be used, it must be added to
     *            a worker.
     */
    static MessageQueue* create(Handler* pHandler);

    /**
     * Destructor
     *
     * Removes itself If still added to a worker and closes the pipe.
     */
    ~MessageQueue();

    /**
     * Posts a message over the queue to the handler provided when the
     * @c MessageQueue was created.
     *
     * @param message  The message to be posted. A bitwise copy of the message
     *                 will be delivered to the handler, after an unspecified time.
     *
     * @return True if the message could be posted, false otherwise. Note that
     *         a return value of true only means that the message could successfully
     *         be posted, not that it has reached the handler.
     *
     * @attention Note that the message queue must have been added to a worker
     *            before a message can be posted.
     */
    bool post(const Message& message) const;

    /**
     * Adds the message queue to a particular worker.
     *
     * @param pWorker  The worker the message queue should be added to.
     *                 Must remain valid until the message queue is removed
     *                 from it.
     *
     * @return True if the message queue could be added, otherwise false.
     *
     * @attention If the message queue is currently added to a worker, it
     *            will first be removed from that worker.
     */
    bool add_to_worker(Worker* pWorker);

    /**
     * Removes the message queue from the worker it is currently added to.
     *
     * @return The worker the message queue was associated with, or NULL
     *         if it was not associated with any.
     */
    Worker* remove_from_worker();

public:
    // TODO: Make private once all callers have been modified.
    friend class MaxBase;
    static bool init();
    static void finish();

private:
    MessageQueue(Handler* pHandler, int read_fd, int write_fd);

    uint32_t handle_poll_events(Worker* pWorker, uint32_t events);

    static uint32_t poll_handler(MXB_POLL_DATA* pData, MXB_WORKER* worker, uint32_t events);

private:
    Handler& m_handler;
    int      m_read_fd;
    int      m_write_fd;
    Worker*  m_pWorker;
};

}
