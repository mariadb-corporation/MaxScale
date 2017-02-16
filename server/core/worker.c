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

#include "maxscale/worker.h"
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <maxscale/alloc.h>
#include <maxscale/config.h>
#include <maxscale/log_manager.h>
#include "maxscale/modules.h"
#include "maxscale/poll.h"

/**
 * Unit variables.
 */
static struct worker_unit
{
    int          n_workers;
    MXS_WORKER** workers;
} this_unit;

/**
 * Structure used for sending cross-thread messages.
 */
typedef struct worker_message
{
    int      id;   /*< Message id. */
    intptr_t arg1; /*< Message specific first argument. */
    intptr_t arg2; /*< Message specific second argument. */
} WORKER_MESSAGE;


static MXS_WORKER* worker_create(int worker_id);
static void worker_free(MXS_WORKER* worker);
static void worker_message_handler(MXS_WORKER* worker, uint32_t msg_id, intptr_t arg1, intptr_t arg2);
static uint32_t worker_poll_handler(MXS_POLL_DATA *data, int worker_id, uint32_t events);
static void worker_thread_main(void* arg);

static bool modules_thread_init();
static void modules_thread_finish();


void mxs_worker_init()
{
    this_unit.n_workers = config_threadcount();
    this_unit.workers = (MXS_WORKER**) MXS_CALLOC(this_unit.n_workers, sizeof(MXS_WORKER*));

    if (!this_unit.workers)
    {
        exit(-1);
    }

    for (int i = 0; i < this_unit.n_workers; ++i)
    {
        MXS_WORKER* worker = worker_create(i);

        if (worker)
        {
            this_unit.workers[i] = worker;
        }
        else
        {
            exit(-1);
        }
    }

    MXS_NOTICE("Workers created!");
}

void mxs_worker_finish()
{
    for (int i = 0; i < this_unit.n_workers; ++i)
    {
        MXS_WORKER* worker = this_unit.workers[i];

        worker_free(worker);
        this_unit.workers[i] = NULL;
    }
}

MXS_WORKER* mxs_worker_get(int worker_id)
{
    ss_dassert(worker_id < this_unit.n_workers);

    return this_unit.workers[worker_id];
}

bool mxs_worker_post_message(MXS_WORKER *worker, uint32_t msg_id, intptr_t arg1, intptr_t arg2)
{
    // NOTE: No logging here, this function must be signal safe.

    WORKER_MESSAGE message = { .id = msg_id, .arg1 = arg1, .arg2 = arg2 };

    ssize_t n = write(worker->write_fd, &message, sizeof(message));

    return n == sizeof(message) ? true : false;
}

size_t mxs_worker_broadcast_message(uint32_t msg_id, intptr_t arg1, intptr_t arg2)
{
    // NOTE: No logging here, this function must be signal safe.

    size_t n = 0;

    for (int i = 0; i < this_unit.n_workers; ++i)
    {
        MXS_WORKER* worker = this_unit.workers[i];

        if (mxs_worker_post_message(worker, msg_id, arg1, arg2))
        {
            ++n;
        }
    }

    return n;
}

void mxs_worker_main(MXS_WORKER* worker)
{
    poll_waitevents(worker);

    MXS_NOTICE("Worker %d has shut down.", worker->id);
}

bool mxs_worker_start(MXS_WORKER* worker)
{
    if (thread_start(&worker->thread, worker_thread_main, worker))
    {
        worker->started = true;
    }

    return worker->started;
}

void mxs_worker_join(MXS_WORKER* worker)
{
    if (worker->started)
    {
        MXS_NOTICE("Waiting for worker %d.", worker->id);
        thread_wait(worker->thread);
        MXS_NOTICE("Waited for worker %d.", worker->id);
        worker->started = false;
    }
}

void mxs_worker_shutdown(MXS_WORKER* worker)
{
    // NOTE: No logging here, this function must be signal safe.

    if (!worker->shutdown_initiated)
    {
        if (mxs_worker_post_message(worker, MXS_WORKER_MSG_SHUTDOWN, 0, 0))
        {
            worker->shutdown_initiated = true;
        }
    }
}

void mxs_worker_shutdown_workers()
{
    // NOTE: No logging here, this function must be signal safe.

    for (int i = 0; i < this_unit.n_workers; ++i)
    {
        MXS_WORKER* worker = this_unit.workers[i];

        mxs_worker_shutdown(worker);
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
static MXS_WORKER* worker_create(int worker_id)
{
    MXS_WORKER* worker = (MXS_WORKER*)MXS_CALLOC(1, sizeof(MXS_WORKER));

    if (worker)
    {
        int fds[2];

        // We create the pipe in message mode (O_DIRECT), so that we do
        // not need to deal with partial messages.
        if (pipe2(fds, O_DIRECT | O_NONBLOCK | O_CLOEXEC) == 0)
        {
            int read_fd = fds[0];
            int write_fd = fds[1];

            worker->poll.handler = worker_poll_handler;
            worker->id = worker_id;
            worker->read_fd = read_fd;
            worker->write_fd = write_fd;

            if (poll_add_fd_to_worker(worker->id, worker->read_fd, EPOLLIN, &worker->poll) != 0)
            {
                MXS_ERROR("Could not add read descriptor of worker to poll set: %s", mxs_strerror(errno));
                worker_free(worker);
                worker = NULL;
            }
        }
        else
        {
            MXS_ERROR("Could not create pipe for worker: %s", mxs_strerror(errno));
            MXS_FREE(worker);
            worker = NULL;
        }
    }

    return worker;
}

/**
 * Frees a worker instance.
 *
 * @param worker The worker instance to be freed.
 */
static void worker_free(MXS_WORKER* worker)
{
    if (worker)
    {
        ss_dassert(!worker->started);

        poll_remove_fd_from_worker(worker->id, worker->read_fd);
        close(worker->read_fd);
        close(worker->write_fd);

        MXS_FREE(worker);
    }
}

/**
 * The worker message handler.
 *
 * @param worker  The worker receiving the message.
 * @param msg_id  The message id.
 * @param arg1    Message specific first argument.
 * @param arg2    Message specific second argument.
 */
static void worker_message_handler(MXS_WORKER *worker, uint32_t msg_id, intptr_t arg1, intptr_t arg2)
{
    switch  (msg_id)
    {
    case MXS_WORKER_MSG_PING:
        {
            ss_dassert(arg1 == 0);
            const char* message = arg2 ? (const char*)arg2 : "Alive and kicking";
            MXS_NOTICE("Worker[%d]: %s.", worker->id, message);
            MXS_FREE((void*)arg2);
        }
        break;

    case MXS_WORKER_MSG_SHUTDOWN:
        {
            MXS_NOTICE("Worker %d received shutdown message.", worker->id);
            worker->should_shutdown = true;
        }
        break;

    case MXS_WORKER_MSG_CALL:
        {
            void (*f)(int, void*) = (void (*)(int,void*))arg1;

            f(worker->id, (void*)arg2);
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
static uint32_t worker_poll_handler(MXS_POLL_DATA *data, int thread_id, uint32_t events)
{
    MXS_WORKER *worker = (MXS_WORKER*)data;
    ss_dassert(worker->id == thread_id);

    int rc = MXS_POLL_NOP;

    // We only expect EPOLLIN events.
    ss_dassert(((events & EPOLLIN) != 0) && ((events & ~EPOLLIN) == 0));

    if (events & EPOLLIN)
    {
        WORKER_MESSAGE message;

        ssize_t n;

        do
        {
            n = read(worker->read_fd, &message, sizeof(message));

            if (n == sizeof(message))
            {
                worker_message_handler(worker, message.id, message.arg1, message.arg2);
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
 * The entry point of each worker thread.
 *
 * @param arg A worker.
 */
static void worker_thread_main(void* arg)
{
    if (modules_thread_init())
    {
        MXS_WORKER *worker = (MXS_WORKER*)arg;

        mxs_worker_main(worker);

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
