/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-04-03
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxbase/ccdefs.hh>
#include <mutex>
#include <vector>
#include <maxbase/poll.hh>

namespace maxbase
{

class MessageQueue;
class Worker;

/**
 * An instance of @c MessageQueueMessage can be sent over a @c MessageQueue from
 * one context to another. The instance will be copied verbatim without any
 * interpretation, so if the same message is sent to multiple recipients it is
 * the caller's and recipient's responsibility to manage the lifetime and
 * concurrent access of anything possibly pointed to from the message.
 */
class MessageQueueMessage   /* final */
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
 * The class @c MessageQueue provides a cross thread message queue.
 */
class MessageQueue : protected mxb::Pollable
{
public:
    using Handler = MessageQueueHandler;
    using Message = MessageQueueMessage;

    enum Kind
    {
        EVENT,
        PIPE
    };

    MessageQueue(const MessageQueue&) = delete;
    MessageQueue& operator=(const MessageQueue&) = delete;

    /**
     * Creates a @c MessageQueue with the provided handler.
     *
     * @param kind      What kind of message queue.
     * @param pHandler  The handler that will receive the messages sent over the
     *                  message queue. Note that the handler *must* remain valid
     *                  for the lifetime of the @c EventMessageQueue.
     *
     * @return A pointer to a new @c MessageQueue or NULL if an error occurred.
     *
     * @attention Before the message queue can be used, it must be added to
     *            a worker.
     */
    static MessageQueue* create(Kind kind, Handler* pHandler);


    /**
     * Removes itself if still added to a worker, and closes all descriptors.
     */
    virtual ~MessageQueue();

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
    virtual bool post(const Message& message) = 0;

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
    virtual bool add_to_worker(Worker* pWorker) = 0;

    /**
     * Removes the message queue from the worker it is currently added to.
     *
     * @return The worker the message queue was associated with, or NULL
     *         if it was not associated with any.
     */
    virtual Worker* remove_from_worker() = 0;

protected:
    MessageQueue();
};

/**
 * The class @c EventMessageQueue provides a message queue implemented on top of @c eventfd.
 */
class EventMessageQueue : public MessageQueue
{
public:
    EventMessageQueue(const EventMessageQueue&) = delete;
    EventMessageQueue& operator=(const EventMessageQueue&) = delete;

    static EventMessageQueue* create(Handler* pHandler);

    ~EventMessageQueue();

    bool post(const Message& message) override;

    bool add_to_worker(Worker* pWorker) override;

    Worker* remove_from_worker() override;

private:
    EventMessageQueue(Handler* pHandler, int event_fd);

    int poll_fd() const override
    {
        return m_event_fd;
    }
    uint32_t handle_poll_events(Worker* pWorker, uint32_t events, Pollable::Context context) override;
    void     swap_messages_and_work();
    void     add_message(const Message& message);

    using MessageVector = std::vector<Message>;

    Handler&      m_handler;
    Worker*       m_pWorker {nullptr};
    int           m_event_fd {-1};  /**< Event file descriptor */
    std::mutex    m_messages_lock;  /**< Protects access to message queue */
    MessageVector m_messages;       /**< Message queue. Accessed from multiple threads. */
    MessageVector m_work;           /**< Work array for messages. Only accessed in the worker thread. */

#ifdef SS_DEBUG
    // Statistics data. May be useful when running in a debugger.
    uint64_t m_total_msgs {0};          /**< Total number of messages seen */
    uint64_t m_single_msg_events {0};   /**< Number of events with one message in queue */
    uint64_t m_multi_msg_events {0};    /**< Number of events with multiple messages in queue */
    uint64_t m_max_msgs_seen {0};       /**< Maximum number of messages in a single event */
    uint64_t m_total_events {0};        /**< Total number of I/O events */
    double   m_ave_msgs_per_event {0};  /**< Average number of messages per I/O event */
#endif
};


/**
 * The class @c PipeMessageQueue provides a MessageQueue implemented on top of a pipe.
 */
class PipeMessageQueue : public MessageQueue
{
public:
    PipeMessageQueue(const PipeMessageQueue&) = delete;
    PipeMessageQueue& operator=(const PipeMessageQueue&) = delete;

    static PipeMessageQueue* create(Handler* pHandler);

    ~PipeMessageQueue();

    bool post(const Message& message) override;

    bool add_to_worker(Worker* pWorker) override;

    Worker* remove_from_worker() override;

private:
    friend class Initer;
    static bool init();
    static void finish();

private:
    PipeMessageQueue(Handler* pHandler, int read_fd, int write_fd);

    int poll_fd() const override
    {
        return m_read_fd;
    }
    uint32_t handle_poll_events(Worker* pWorker, uint32_t events, Pollable::Context context) override;

private:
    Handler& m_handler;
    int      m_read_fd;
    int      m_write_fd;
    Worker*  m_pWorker { nullptr };
};
}
