/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2019-07-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "maxscale/worker.hh"
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <maxscale/alloc.h>
#include <maxscale/config.h>
#include <maxscale/log_manager.h>
#include <maxscale/platform.h>
#include "maxscale/modules.h"
#include "maxscale/poll.h"

#define WORKER_ABSENT_ID -1

using maxscale::Worker;

/**
 * Unit variables.
 */
static struct this_unit
{
    int      n_workers; // How many workers there are.
    Worker** ppWorkers; // Array of worker instances.
} this_unit =
{
    0,
    NULL
};

static thread_local struct this_thread
{
    int current_worker_id; // The worker id of the current thread
} this_thread =
{
    WORKER_ABSENT_ID
};

/**
 * Structure used for sending cross-thread messages.
 */
typedef struct worker_message
{
    uint32_t id;   /*< Message id. */
    intptr_t arg1; /*< Message specific first argument. */
    intptr_t arg2; /*< Message specific second argument. */
} WORKER_MESSAGE;


static MXS_WORKER* worker_create(int worker_id);
static void worker_free(MXS_WORKER* worker);
static uint32_t worker_poll_handler(MXS_POLL_DATA *data, int worker_id, uint32_t events);

static bool modules_thread_init();
static void modules_thread_finish();

Worker::Worker(int id, int read_fd, int write_fd)
{
    m_poll.handler = &Worker::poll_handler;
    m_id = id;
    m_read_fd = read_fd;
    m_write_fd = write_fd;
    m_thread = 0;
    m_started = false;
    m_should_shutdown = false;
    m_shutdown_initiated = false;
}

// static
void Worker::init()
{
    this_unit.n_workers = config_threadcount();
    this_unit.ppWorkers = new (std::nothrow) Worker* (); // Zero initialized array

    if (!this_unit.ppWorkers)
    {
        // If we cannot allocate the array, we just exit.
        exit(-1);
    }

    for (int i = 0; i < this_unit.n_workers; ++i)
    {
        Worker* pWorker = Worker::create(i);

        if (pWorker)
        {
            this_unit.ppWorkers[i] = pWorker;
        }
        else
        {
            // If a worker cannot be created, we just exit. No way we can continue.
            exit(-1);
        }
    }

    MXS_NOTICE("Workers created!");
}

void mxs_worker_init()
{
    Worker::init();
}

void Worker::finish()
{
    for (int i = 0; i < this_unit.n_workers; ++i)
    {
        Worker* pWorker = this_unit.ppWorkers[i];

        delete pWorker;
        this_unit.ppWorkers[i] = NULL;
    }
}

void mxs_worker_finish()
{
    Worker::finish();
}

Worker* Worker::get(int worker_id)
{
    ss_dassert(worker_id < this_unit.n_workers);

    return this_unit.ppWorkers[worker_id];
}

MXS_WORKER* mxs_worker_get(int worker_id)
{
    return Worker::get(worker_id);
}

MXS_WORKER* mxs_worker_get_current()
{
    MXS_WORKER* worker = NULL;

    int worker_id = this_thread.current_worker_id;

    if (worker_id != WORKER_ABSENT_ID)
    {
        worker = mxs_worker_get(worker_id);
    }

    return worker;
}

int mxs_worker_get_current_id()
{
    return this_thread.current_worker_id;
}

bool Worker::post_message(uint32_t msg_id, intptr_t arg1, intptr_t arg2)
{
    // NOTE: No logging here, this function must be signal safe.
    WORKER_MESSAGE message = { .id = msg_id, .arg1 = arg1, .arg2 = arg2 };

    ssize_t n = write(m_write_fd, &message, sizeof(message));

    return n == sizeof(message) ? true : false;
}

bool mxs_worker_post_message(MXS_WORKER* pWorker, uint32_t msg_id, intptr_t arg1, intptr_t arg2)
{
    return static_cast<Worker*>(pWorker)->post_message(msg_id, arg1, arg2);
}

size_t Worker::broadcast_message(uint32_t msg_id, intptr_t arg1, intptr_t arg2)
{
    // NOTE: No logging here, this function must be signal safe.

    size_t n = 0;

    for (int i = 0; i < this_unit.n_workers; ++i)
    {
        Worker* pWorker = this_unit.ppWorkers[i];

        if (pWorker->post_message(msg_id, arg1, arg2))
        {
            ++n;
        }
    }

    return n;
}

size_t mxs_worker_broadcast_message(uint32_t msg_id, intptr_t arg1, intptr_t arg2)
{
    return Worker::broadcast_message(msg_id, arg1, arg2);
}

void Worker::run()
{
    this_thread.current_worker_id = m_id;
    poll_waitevents(this);
    this_thread.current_worker_id = WORKER_ABSENT_ID;

    MXS_NOTICE("Worker %d has shut down.", m_id);
}

void mxs_worker_main(MXS_WORKER* pWorker)
{
    return static_cast<Worker*>(pWorker)->run();
}

bool Worker::start()
{
    m_started = true;

    if (!thread_start(&m_thread, &Worker::thread_main, this))
    {
        m_started = false;
    }

    return m_started;
}

bool mxs_worker_start(MXS_WORKER* pWorker)
{
    return static_cast<Worker*>(pWorker)->start();
}

void Worker::join()
{
    if (m_started)
    {
        MXS_NOTICE("Waiting for worker %d.", m_id);
        thread_wait(m_thread);
        MXS_NOTICE("Waited for worker %d.", m_id);
        m_started = false;
    }
}

void mxs_worker_join(MXS_WORKER* pWorker)
{
    static_cast<Worker*>(pWorker)->join();
}

void Worker::shutdown()
{
    // NOTE: No logging here, this function must be signal safe.

    if (!m_shutdown_initiated)
    {
        if (mxs_worker_post_message(this, MXS_WORKER_MSG_SHUTDOWN, 0, 0))
        {
            m_shutdown_initiated = true;
        }
    }
}

void mxs_worker_shutdown(MXS_WORKER* pWorker)
{
    static_cast<Worker*>(pWorker)->shutdown();
}

void Worker::shutdown_all()
{
    // NOTE: No logging here, this function must be signal safe.

    for (int i = 0; i < this_unit.n_workers; ++i)
    {
        Worker* pWorker = this_unit.ppWorkers[i];

        pWorker->shutdown();
    }
}

void mxs_worker_shutdown_workers()
{
    return Worker::shutdown_all();
}

/**
 * Creates a worker instance.
 * - Allocates the structure.
 * - Creates a pipe.
 * - Adds the read descriptor to the polling mechanism.
 *
 * @param worker_id  The id of the worker.
 *
 * @return A worker instance if successful, otherwise NULL.
 */
//static
Worker* Worker::create(int worker_id)
{
    Worker* pWorker = NULL;

    int fds[2];

    // We create the pipe in message mode (O_DIRECT), so that we do
    // not need to deal with partial messages.
    if (pipe2(fds, O_DIRECT | O_NONBLOCK | O_CLOEXEC) == 0)
    {
        int read_fd = fds[0];
        int write_fd = fds[1];

        pWorker = new (std::nothrow) Worker(worker_id, read_fd, write_fd);

        if (pWorker)
        {
            if (poll_add_fd_to_worker(worker_id, read_fd, EPOLLIN, &pWorker->m_poll) != 0)
            {
                MXS_ERROR("Could not add read descriptor of worker to poll set: %s", mxs_strerror(errno));
                delete pWorker;
                pWorker = NULL;
            }
        }
    }
    else
    {
        MXS_ERROR("Could not create pipe for worker: %s", mxs_strerror(errno));
    }

    return pWorker;
}

/**
 * Frees a worker instance.
 *
 * @param worker The worker instance to be freed.
 */
Worker::~Worker()
{
    ss_dassert(!m_started);

    poll_remove_fd_from_worker(m_id, m_read_fd);
    close(m_read_fd);
    close(m_write_fd);
}

/**
 * The worker message handler.
 *
 * @param worker  The worker receiving the message.
 * @param msg_id  The message id.
 * @param arg1    Message specific first argument.
 * @param arg2    Message specific second argument.
 */
void Worker::handle_message(uint32_t msg_id, intptr_t arg1, intptr_t arg2)
{
    switch  (msg_id)
    {
    case MXS_WORKER_MSG_PING:
        {
            ss_dassert(arg1 == 0);
            char* zArg2 = reinterpret_cast<char*>(arg2);
            const char* zMessage = zArg2 ? zArg2 : "Alive and kicking";
            MXS_NOTICE("Worker[%d]: %s.", m_id, zMessage);
            MXS_FREE(zArg2);
        }
        break;

    case MXS_WORKER_MSG_SHUTDOWN:
        {
            MXS_NOTICE("Worker %d received shutdown message.", m_id);
            m_should_shutdown = true;
        }
        break;

    case MXS_WORKER_MSG_CALL:
        {
            void (*f)(int, void*) = (void (*)(int,void*))arg1;

            f(m_id, (void*)arg2);
        }
        break;

    default:
        MXS_ERROR("Worker received unknown message %d.", msg_id);
    }
}

/**
 * Handler for poll events related to the read descriptor of the worker.
 *
 * @param data       Pointer to the MXS_POLL_DATA member of the MXS_WORKER.
 * @param thread_id  Id of the thread; same as id of the relevant worker.
 * @param events     Epoll events.
 *
 * @return What events the handler handled.
 */
uint32_t Worker::poll(uint32_t events)
{
    int rc = MXS_POLL_NOP;

    // We only expect EPOLLIN events.
    ss_dassert(((events & EPOLLIN) != 0) && ((events & ~EPOLLIN) == 0));

    if (events & EPOLLIN)
    {
        WORKER_MESSAGE message;

        ssize_t n;

        do
        {
            n = read(m_read_fd, &message, sizeof(message));

            if (n == sizeof(message))
            {
                handle_message(message.id, message.arg1, message.arg2);
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
                MXS_ERROR("Worker could only read %ld bytes from pipe, although expected %lu bytes.",
                          n, sizeof(message));
                ss_dassert(!true);
            }
        }
        while ((n != 0) && (n != -1));

        rc = MXS_POLL_READ;
    }

    return rc;
}

//static
uint32_t Worker::poll_handler(MXS_POLL_DATA* pData, int thread_id, uint32_t events)
{
    Worker* pWorker = reinterpret_cast<Worker*>(pData);
    ss_dassert(pWorker->m_id == thread_id);

    return pWorker->poll(events);
}

/**
 * The entry point of each worker thread.
 *
 * @param arg A worker.
 */
//static
void Worker::thread_main(void* pArg)
{
    if (modules_thread_init())
    {
        Worker* pWorker = static_cast<Worker*>(pArg);

        pWorker->run();

        modules_thread_finish();
    }
    else
    {
        MXS_ERROR("Could not perform thread initialization for all modules. Thread exits.");
    }
}

/**
 * Calls thread_init on all loaded modules.
 *
 * @return True, if all modules were successfully initialized.
 */
static bool modules_thread_init()
{
    bool initialized = false;

    MXS_MODULE_ITERATOR i = mxs_module_iterator_get(NULL);
    MXS_MODULE* module = NULL;

    while ((module = mxs_module_iterator_get_next(&i)) != NULL)
    {
        if (module->thread_init)
        {
            int rc = (module->thread_init)();

            if (rc != 0)
            {
                break;
            }
        }
    }

    if (module)
    {
        // If module is non-NULL it means that the initialization failed for
        // that module. We now need to call finish on all modules that were
        // successfully initialized.
        MXS_MODULE* failed_module = module;
        i = mxs_module_iterator_get(NULL);

        while ((module = mxs_module_iterator_get_next(&i)) != failed_module)
        {
            if (module->thread_finish)
            {
                (module->thread_finish)();
            }
        }
    }
    else
    {
        initialized = true;
    }

    return initialized;
}

/**
 * Calls thread_finish on all loaded modules.
 */
static void modules_thread_finish()
{
    MXS_MODULE_ITERATOR i = mxs_module_iterator_get(NULL);
    MXS_MODULE* module = NULL;

    while ((module = mxs_module_iterator_get_next(&i)) != NULL)
    {
        if (module->thread_finish)
        {
            (module->thread_finish)();
        }
    }
}
