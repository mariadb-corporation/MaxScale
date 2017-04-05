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
#include <signal.h>
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

namespace
{

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

/**
 * Check error returns from epoll_ctl; impossible ones lead to crash.
 *
 * @param wid        Worker id.
 * @param errornum   The errno set by epoll_ctl
 * @param op         Either EPOLL_CTL_ADD or EPOLL_CTL_DEL.
 */
void poll_resolve_error(int wid, int fd, int errornum, int op)
{
    if (op == EPOLL_CTL_ADD)
    {
        if (EEXIST == errornum)
        {
            MXS_ERROR("File descriptor %d already added to epoll instance of worker %d.", fd, wid);
            return;
        }

        if (ENOSPC == errornum)
        {
            MXS_ERROR("The limit imposed by /proc/sys/fs/epoll/max_user_watches was "
                      "reached when trying to add file descriptor %d to epoll instance "
                      "of worker %d.", fd, wid);
            return;
        }
    }
    else
    {
        ss_dassert(op == EPOLL_CTL_DEL);

        /* Must be removing */
        if (ENOENT == errornum)
        {
            MXS_ERROR("File descriptor %d was not found in epoll instance of worker %d.", fd, wid);
            return;
        }
    }

    /* Common checks for add or remove - crash MaxScale */
    if (EBADF == errornum)
    {
        raise(SIGABRT);
    }
    if (EINVAL == errornum)
    {
        raise(SIGABRT);
    }
    if (ENOMEM == errornum)
    {
        raise(SIGABRT);
    }
    if (EPERM == errornum)
    {
        raise(SIGABRT);
    }

    /* Undocumented error number */
    raise(SIGABRT);
}

}

static bool modules_thread_init();
static void modules_thread_finish();

Worker::Worker(int id, int epoll_fd, int read_fd, int write_fd)
    : m_id(id)
    , m_epoll_fd(epoll_fd)
    , m_read_fd(read_fd)
    , m_write_fd(write_fd)
{
    m_poll.handler = &Worker::poll_handler;

    m_thread = 0;
    m_started = false;
    m_should_shutdown = false;
    m_shutdown_initiated = false;
}

Worker::~Worker()
{
    ss_dassert(!m_started);

    poll_remove_fd_from_worker(m_id, m_read_fd);
    close(m_read_fd);
    close(m_write_fd);
    close(m_epoll_fd);
}

// static
void Worker::init()
{
    this_unit.n_workers = config_threadcount();
    this_unit.ppWorkers = new (std::nothrow) Worker* [this_unit.n_workers] (); // Zero initialized array

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

void Worker::finish()
{
    for (int i = 0; i < this_unit.n_workers; ++i)
    {
        Worker* pWorker = this_unit.ppWorkers[i];

        delete pWorker;
        this_unit.ppWorkers[i] = NULL;
    }
}

bool Worker::add_fd(int fd, uint32_t events, MXS_POLL_DATA* pData)
{
    bool rv = true;

    events |= EPOLLET;

    struct epoll_event ev;

    ev.events = events;
    ev.data.ptr = pData;

    pData->thread.id = m_id;

    if (epoll_ctl(m_epoll_fd, EPOLL_CTL_ADD, fd, &ev) != 0)
    {
        poll_resolve_error(m_id, fd, errno, EPOLL_CTL_ADD);
        rv = false;
    }

    return rv;
}

bool Worker::remove_fd(int fd)
{
    bool rv = true;

    struct epoll_event ev = {};

    if (epoll_ctl(m_epoll_fd, EPOLL_CTL_DEL, fd, &ev) != 0)
    {
        poll_resolve_error(m_id, fd, errno, EPOLL_CTL_DEL);
        rv = false;
    }

    return rv;
}

int mxs_worker_id(MXS_WORKER* pWorker)
{
    return static_cast<Worker*>(pWorker)->id();
}

bool mxs_worker_should_shutdown(MXS_WORKER* pWorker)
{
    return static_cast<Worker*>(pWorker)->should_shutdown();
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

Worker* Worker::get_current()
{
    Worker* pWorker = NULL;

    int worker_id = get_current_id();

    if (worker_id != WORKER_ABSENT_ID)
    {
        pWorker = Worker::get(worker_id);
    }

    return pWorker;
}

int Worker::get_current_id()
{
    return this_thread.current_worker_id;
}

bool Worker::post_message(uint32_t msg_id, intptr_t arg1, intptr_t arg2)
{
    // NOTE: No logging here, this function must be signal safe.
    WORKER_MESSAGE message = {};
    message.id = msg_id;
    message.arg1 = arg1;
    message.arg2 = arg2;

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

bool Worker::start()
{
    m_started = true;

    if (!thread_start(&m_thread, &Worker::thread_main, this))
    {
        m_started = false;
    }

    return m_started;
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

void Worker::shutdown()
{
    // NOTE: No logging here, this function must be signal safe.

    if (!m_shutdown_initiated)
    {
        if (post_message(MXS_WORKER_MSG_SHUTDOWN, 0, 0))
        {
            m_shutdown_initiated = true;
        }
    }
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

    int epoll_fd = epoll_create(MAX_EVENTS);

    if (epoll_fd != -1)
    {
        int fds[2];

        // We create the pipe in message mode (O_DIRECT), so that we do
        // not need to deal with partial messages.
        if (pipe2(fds, O_DIRECT | O_NONBLOCK | O_CLOEXEC) == 0)
        {
            int read_fd = fds[0];
            int write_fd = fds[1];

            pWorker = new (std::nothrow) Worker(worker_id, epoll_fd, read_fd, write_fd);

            if (pWorker)
            {
                if (poll_add_fd_to_worker(worker_id, read_fd, EPOLLIN, &pWorker->m_poll) != 0)
                {
                    MXS_ERROR("Could not add read descriptor of worker to poll set: %s", mxs_strerror(errno));
                    delete pWorker;
                    pWorker = NULL;
                }
            }
            else
            {
                close(read_fd);
                close(write_fd);
                close(epoll_fd);
            }
        }
        else
        {
            MXS_ERROR("Could not create pipe for worker: %s", mxs_strerror(errno));
        }
    }
    else
    {
        MXS_ERROR("Could not create epoll-instance for worker: %s", mxs_strerror(errno));
    }

    return pWorker;
}

/**
 * The worker message handler.
 *
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
 * Worker poll handler.
 *
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

/**
 * Handler for poll events related to the read descriptor of the worker.
 *
 * @param pData      The MXS_POLL_DATA of the worker in question.
 * @param thread_id  Id of the thread; same as id of the relevant worker.
 * @param events     Epoll events.
 *
 * @return What events the handler handled.
 */
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
