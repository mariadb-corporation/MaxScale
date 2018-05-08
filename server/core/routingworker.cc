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

#include "internal/routingworker.hh"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <vector>
#include <sstream>

#include <maxscale/alloc.h>
#include <maxscale/atomic.h>
#include <maxscale/config.h>
#include <maxscale/clock.h>
#include <maxscale/limits.h>
#include <maxscale/platform.h>
#include <maxscale/semaphore.hh>
#include <maxscale/json_api.h>
#include <maxscale/utils.hh>

#include "internal/dcb.h"
#include "internal/modules.h"
#include "internal/poll.h"
#include "internal/service.h"
#include "internal/statistics.h"

#define WORKER_ABSENT_ID -1

using maxscale::RoutingWorker;
using maxscale::WorkerLoad;
using maxscale::Closer;
using maxscale::Semaphore;
using std::vector;
using std::stringstream;

namespace
{

const int MXS_WORKER_MSG_TASK = -1;
const int MXS_WORKER_MSG_DISPOSABLE_TASK = -2;

/**
 * Unit variables.
 */
struct this_unit
{
    bool            initialized;       // Whether the initialization has been performed.
    int             nWorkers;          // How many routing workers there are.
    RoutingWorker** ppWorkers;         // Array of routing worker instances.
    // DEPRECATED in 2.3, remove in 2.4.
    int             number_poll_spins; // Maximum non-block polls
    // DEPRECATED in 2.3, remove in 2.4.
    int             max_poll_sleep;    // Maximum block time
    int             epoll_listener_fd; // Shared epoll descriptor for listening descriptors.
    int             id_main_worker;    // The id of the worker running in the main thread.
    int             id_min_worker;     // The smallest routing worker id.
    int             id_max_worker;     // The largest routing worker id.
} this_unit =
{
    false,            // initialized
    0,                // nWorkers
    NULL,             // ppWorkers
    0,                // number_poll_spins
    0,                // max_poll_sleep
    -1,               // epoll_listener_fd
    WORKER_ABSENT_ID, // id_main_worker
    WORKER_ABSENT_ID, // id_min_worker
    WORKER_ABSENT_ID, // id_max_worker
};

thread_local struct this_thread
{
    int current_worker_id; // The worker id of the current thread
} this_thread =
{
    WORKER_ABSENT_ID
};

/**
 * Calls thread_init on all loaded modules.
 *
 * @return True, if all modules were successfully initialized.
 */
bool modules_thread_init()
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
void modules_thread_finish()
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

}

namespace maxscale
{

RoutingWorker::RoutingWorker()
{
    MXS_POLL_DATA::handler = &RoutingWorker::epoll_instance_handler;
    MXS_POLL_DATA::thread.id = m_id;
}

RoutingWorker::~RoutingWorker()
{
}

// static
bool RoutingWorker::init()
{
    ss_dassert(!this_unit.initialized);

    this_unit.number_poll_spins = config_nbpolls();
    this_unit.max_poll_sleep = config_pollsleep();

    this_unit.epoll_listener_fd = epoll_create(MAX_EVENTS);

    if (this_unit.epoll_listener_fd != -1)
    {
        int nWorkers = config_threadcount();
        RoutingWorker** ppWorkers = new (std::nothrow) RoutingWorker* [MXS_MAX_THREADS] (); // 0-inited array

        if (ppWorkers)
        {
            int id_main_worker = WORKER_ABSENT_ID;
            int id_min_worker = INT_MAX;
            int id_max_worker = INT_MIN;

            int i;
            for (i = 0; i < nWorkers; ++i)
            {
                RoutingWorker* pWorker = RoutingWorker::create(this_unit.epoll_listener_fd);

                if (pWorker)
                {
                    int id = pWorker->id();

                    // The first created worker will be the main worker.
                    if (id_main_worker == WORKER_ABSENT_ID)
                    {
                        id_main_worker = id;
                    }

                    if (id < id_min_worker)
                    {
                        id_min_worker = id;
                    }

                    if (id > id_max_worker)
                    {
                        id_max_worker = id;
                    }

                    ppWorkers[i] = pWorker;
                }
                else
                {
                    for (int j = i - 1; j >= 0; --j)
                    {
                        delete ppWorkers[j];
                    }

                    delete [] ppWorkers;
                    ppWorkers = NULL;
                    break;
                }
            }

            if (ppWorkers)
            {
                this_unit.ppWorkers = ppWorkers;
                this_unit.nWorkers = nWorkers;
                this_unit.id_main_worker = id_main_worker;
                this_unit.id_min_worker = id_min_worker;
                this_unit.id_max_worker = id_max_worker;

                this_unit.initialized = true;
            }
        }
        else
        {
            MXS_OOM();
            close(this_unit.epoll_listener_fd);
        }
    }
    else
    {
        MXS_ALERT("Could not allocate an epoll instance.");
    }

    if (this_unit.initialized)
    {
        // When the initialization has successfully been performed, we set the
        // current_worker_id of this thread to 0. That way any connections that
        // are made during service startup (after this function returns, but
        // bofore the workes have been started) will be handled by the worker
        // that will be running in the main thread.
        this_thread.current_worker_id = 0;
    }

    return this_unit.initialized;
}

void RoutingWorker::finish()
{
    ss_dassert(this_unit.initialized);

    for (int i = this_unit.id_max_worker; i >= this_unit.id_min_worker; --i)
    {
        RoutingWorker* pWorker = this_unit.ppWorkers[i];
        ss_dassert(pWorker);

        delete pWorker;
        this_unit.ppWorkers[i] = NULL;
    }

    delete [] this_unit.ppWorkers;
    this_unit.ppWorkers = NULL;

    close(this_unit.epoll_listener_fd);
    this_unit.epoll_listener_fd = 0;

    this_unit.initialized = false;
}

//static
bool RoutingWorker::add_shared_fd(int fd, uint32_t events, MXS_POLL_DATA* pData)
{
    bool rv = true;

    // This must be level-triggered. Since this is intended for listening
    // sockets and each worker will call accept() just once before going
    // back the epoll_wait(), using EPOLLET would mean that if there are
    // more clients to be accepted than there are threads returning from
    // epoll_wait() for an event, then some clients would be accepted only
    // when a new client has connected, thus causing a new EPOLLIN event.
    events &= ~EPOLLET;

    struct epoll_event ev;

    ev.events = events;
    ev.data.ptr = pData;

    pData->thread.id = 0; // TODO: Remove the thread id altogether.

    if (epoll_ctl(this_unit.epoll_listener_fd, EPOLL_CTL_ADD, fd, &ev) != 0)
    {
        Worker::resolve_poll_error(fd, errno, EPOLL_CTL_ADD);
        rv = false;
    }

    return rv;
}

//static
bool RoutingWorker::remove_shared_fd(int fd)
{
    bool rv = true;

    struct epoll_event ev = {};

    if (epoll_ctl(this_unit.epoll_listener_fd, EPOLL_CTL_DEL, fd, &ev) != 0)
    {
        Worker::resolve_poll_error(fd, errno, EPOLL_CTL_DEL);
        rv = false;
    }

    return rv;
}

int mxs_worker_id(MXS_WORKER* pWorker)
{
    return static_cast<RoutingWorker*>(pWorker)->id();
}

bool mxs_worker_should_shutdown(MXS_WORKER* pWorker)
{
    return static_cast<RoutingWorker*>(pWorker)->should_shutdown();
}

RoutingWorker* RoutingWorker::get(int worker_id)
{
    if (worker_id == MAIN)
    {
        worker_id = this_unit.id_main_worker;
    }

    ss_dassert((worker_id >= this_unit.id_min_worker) && (worker_id <= this_unit.id_max_worker));

    return this_unit.ppWorkers[worker_id];
}

RoutingWorker* RoutingWorker::get_current()
{
    RoutingWorker* pWorker = NULL;

    int worker_id = get_current_id();

    if (worker_id != WORKER_ABSENT_ID)
    {
        pWorker = RoutingWorker::get(worker_id);
    }

    return pWorker;
}

int RoutingWorker::get_current_id()
{
    return this_thread.current_worker_id;
}

//static
bool RoutingWorker::start_threaded_workers()
{
    bool rv = true;
    size_t stack_size = config_thread_stack_size();

    for (int i = this_unit.id_min_worker; i <= this_unit.id_max_worker; ++i)
    {
        // The main RoutingWorker will run in the main thread, so
        // we exclude that.
        if (i != this_unit.id_main_worker)
        {
            RoutingWorker* pWorker = this_unit.ppWorkers[i];
            ss_dassert(pWorker);

            if (!pWorker->start(stack_size))
            {
                MXS_ALERT("Could not start routing worker %d of %d.", i, config_threadcount());
                rv = false;
                // At startup, so we don't even try to clean up.
                break;
            }
        }
    }

    return rv;
}

//static
void RoutingWorker::join_threaded_workers()
{
    for (int i = this_unit.id_min_worker; i <= this_unit.id_max_worker; i++)
    {
        if (i != this_unit.id_main_worker)
        {
            RoutingWorker* pWorker = this_unit.ppWorkers[i];
            ss_dassert(pWorker);

            pWorker->join();
        }
    }
}

//static
void RoutingWorker::set_nonblocking_polls(unsigned int nbpolls)
{
    this_unit.number_poll_spins = nbpolls;
}

//static
void RoutingWorker::set_maxwait(unsigned int maxwait)
{
    this_unit.max_poll_sleep = maxwait;
}

RoutingWorker::SessionsById& RoutingWorker::session_registry()
{
    return m_sessions;
}

void RoutingWorker::register_zombie(DCB* pDcb)
{
    ss_dassert(pDcb->poll.thread.id == m_id);

    m_zombies.push_back(pDcb);
}

void RoutingWorker::delete_zombies()
{
    // An algorithm cannot be used, as the final closing of a DCB may cause
    // other DCBs to be registered in the zombie queue.

    while (!m_zombies.empty())
    {
        DCB* pDcb = m_zombies.back();
        m_zombies.pop_back();
        dcb_final_close(pDcb);
    }
}

bool RoutingWorker::pre_run()
{
    this_thread.current_worker_id = m_id;

    bool rv = modules_thread_init() && service_thread_init();

    if (!rv)
    {
        MXS_ERROR("Could not perform thread initialization for all modules. Thread exits.");
        this_thread.current_worker_id = WORKER_ABSENT_ID;
    }

    return rv;
}

void RoutingWorker::post_run()
{
    modules_thread_finish();
    // TODO: Add sercice_thread_finish().
    this_thread.current_worker_id = WORKER_ABSENT_ID;
}

/**
 * Creates a worker instance.
 * - Allocates the structure.
 * - Creates a pipe.
 * - Adds the read descriptor to the polling mechanism.
 *
 * @param epoll_listener_fd  The file descriptor of the epoll set to which listening
 *                           sockets will be placed.
 *
 * @return A worker instance if successful, otherwise NULL.
 */
//static
RoutingWorker* RoutingWorker::create(int epoll_listener_fd)
{
    RoutingWorker* pThis = new (std::nothrow) RoutingWorker();

    if (pThis)
    {
        struct epoll_event ev;
        ev.events = EPOLLIN;
        MXS_POLL_DATA* pData = pThis;
        ev.data.ptr = pData; // Necessary for pointer adjustment, otherwise downcast will not work.

        // The shared epoll instance descriptor is *not* added using EPOLLET (edge-triggered)
        // because we want it to be level-triggered. That way, as long as there is a single
        // active (accept() can be called) listening socket, epoll_wait() will return an event
        // for it. It must be like that because each worker will call accept() just once before
        // calling epoll_wait() again. The end result is that as long as the load of different
        // workers is roughly the same, the client connections will be distributed evenly across
        // the workers. If the load is not the same, then a worker with less load will get more
        // clients that a worker with more load.
        if (epoll_ctl(pThis->m_epoll_fd, EPOLL_CTL_ADD, epoll_listener_fd, &ev) == 0)
        {
            MXS_INFO("Epoll instance for listening sockets added to worker epoll instance.");
        }
        else
        {
            MXS_ERROR("Could not add epoll instance for listening sockets to "
                      "epoll instance of worker: %s", mxs_strerror(errno));
            delete pThis;
            pThis = NULL;
        }
    }
    else
    {
        MXS_OOM();
    }

    return pThis;
}

void RoutingWorker::epoll_tick()
{
    dcb_process_idle_sessions(m_id);

    m_state = ZPROCESSING;

    delete_zombies();
}

/**
 * Callback for events occurring on the shared epoll instance.
 *
 * @param pData   Will point to a Worker instance.
 * @param wid     The worker id.
 * @param events  The events.
 *
 * @return What actions were performed.
 */
//static
uint32_t RoutingWorker::epoll_instance_handler(struct mxs_poll_data* pData, int wid, uint32_t events)
{
    RoutingWorker* pWorker = static_cast<RoutingWorker*>(pData);
    ss_dassert(pWorker->m_id == wid);

    return pWorker->handle_epoll_events(events);
}

/**
 * Handler for events occurring in the shared epoll instance.
 *
 * @param events  The events.
 *
 * @return What actions were performed.
 */
uint32_t RoutingWorker::handle_epoll_events(uint32_t events)
{
    struct epoll_event epoll_events[1];

    // We extract just one event
    int nfds = epoll_wait(this_unit.epoll_listener_fd, epoll_events, 1, 0);

    uint32_t actions = MXS_POLL_NOP;

    if (nfds == -1)
    {
        MXS_ERROR("epoll_wait failed: %s", mxs_strerror(errno));
    }
    else if (nfds == 0)
    {
        MXS_DEBUG("No events for worker %d.", m_id);
    }
    else
    {
        MXS_DEBUG("1 event for worker %d.", m_id);
        MXS_POLL_DATA* pData = static_cast<MXS_POLL_DATA*>(epoll_events[0].data.ptr);

        actions = pData->handler(pData, m_id, epoll_events[0].events);
    }

    return actions;
}

}

size_t mxs_rworker_broadcast_message(uint32_t msg_id, intptr_t arg1, intptr_t arg2)
{
    return RoutingWorker::broadcast_message(msg_id, arg1, arg2);
}

bool mxs_rworker_register_session(MXS_SESSION* session)
{
    RoutingWorker* pWorker = RoutingWorker::get_current();
    ss_dassert(pWorker);
    return pWorker->session_registry().add(session);
}

bool mxs_rworker_deregister_session(uint64_t id)
{
    RoutingWorker* pWorker = RoutingWorker::get_current();
    ss_dassert(pWorker);
    return pWorker->session_registry().remove(id);
}

MXS_SESSION* mxs_rworker_find_session(uint64_t id)
{
    RoutingWorker* pWorker = RoutingWorker::get_current();
    ss_dassert(pWorker);
    return pWorker->session_registry().lookup(id);
}

MXS_WORKER* mxs_rworker_get(int worker_id)
{
    return RoutingWorker::get(worker_id);
}

MXS_WORKER* mxs_rworker_get_current()
{
    return RoutingWorker::get_current();
}

int mxs_rworker_get_current_id()
{
    return RoutingWorker::get_current_id();
}
