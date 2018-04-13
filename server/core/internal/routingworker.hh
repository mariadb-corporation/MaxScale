#pragma once
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

#include <maxscale/cppdefs.hh>
#include <maxscale/routingworker.h>
#include "worker.hh"
#include "session.hh"

namespace maxscale
{

class RoutingWorker : public Worker
                    , private MXS_POLL_DATA
{
    RoutingWorker(const RoutingWorker&) = delete;
    RoutingWorker& operator = (const RoutingWorker&) = delete;

public:
    enum
    {
        MAIN = -1
    };

    typedef Registry<MXS_SESSION> SessionsById;
    typedef std::vector<DCB*>     Zombies;

    /**
     * Initialize the routing worker mechanism.
     *
     * To be called once at process startup. This will cause as many workers
     * to be created as the number of threads defined.
     *
     * @return True if the initialization succeeded, false otherwise.
     */
    static bool init();

    /**
     * Finalize the worker mechanism.
     *
     * To be called once at process shutdown. This will cause all workers
     * to be destroyed. When the function is called, no worker should be
     * running anymore.
     */
    static void finish();

    /**
     * Add a file descriptor to the epoll instance shared between all workers.
     * Events occuring on the provided file descriptor will be handled by all
     * workers. This is primarily intended for listening sockets where the
     * only event is EPOLLIN, signaling that accept() can be used on the listening
     * socket for creating a connected socket to a client.
     *
     * @param fd      The file descriptor to be added.
     * @param events  Mask of epoll event types.
     * @param pData   The poll data associated with the descriptor:
     *
     *                  data->handler  : Handler that knows how to deal with events
     *                                   for this particular type of 'struct mxs_poll_data'.
     *                  data->thread.id: 0
     *
     * @return True, if the descriptor could be added, false otherwise.
     */
    static bool add_shared_fd(int fd, uint32_t events, MXS_POLL_DATA* pData);

    /**
     * Remove a file descriptor from the epoll instance shared between all workers.
     *
     * @param fd  The file descriptor to be removed.
     *
     * @return True on success, false on failure.
     */
    static bool remove_shared_fd(int fd);

    /**
     * Register zombie for later deletion.
     *
     * @param pZombie  DCB that will be deleted at end of event loop.
     *
     * @note The DCB must be owned by this worker.
     */
    void register_zombie(DCB* pZombie);

    /**
     * Return a reference to the session registry of this worker.
     *
     * @return Session registry.
     */
    SessionsById& session_registry();

    /**
     * Return the worker associated with the provided worker id.
     *
     * @param worker_id  A worker id. By specifying MAIN, the routing worker
     *                   running in the main thread will be returned.
     *
     * @return The corresponding worker instance, or NULL if the id does
     *         not correspond to a worker.
     */
    static RoutingWorker* get(int worker_id);

    /**
     * Return the worker associated with the current thread.
     *
     * @return The worker instance, or NULL if the current thread does not have a worker.
     */
    static RoutingWorker* get_current();

    /**
     * Return the worker id associated with the current thread.
     *
     * @return A worker instance, or -1 if the current thread does not have a worker.
     */
    static int get_current_id();

    /**
     * Starts all routing workers but the main worker (the one running in
     * the main thread).
     *
     * @return True, if all secondary workers could be started.
     */
    static bool start_threaded_workers();

    /**
     * Waits for all threaded workers.
     */
    static void join_threaded_workers();

    /**
     * Deprecated
     */
    static void set_nonblocking_polls(unsigned int nbpolls);

    /**
     * Deprecated
     */
    static void set_maxwait(unsigned int maxwait);

private:
    RoutingWorker();
    virtual ~RoutingWorker();

    static RoutingWorker* create(int epoll_listener_fd);

    bool pre_run();    // override
    void post_run();   // override
    void epoll_tick(); // override

    void delete_zombies();

    static uint32_t epoll_instance_handler(struct mxs_poll_data* data, int wid, uint32_t events);
    uint32_t handle_epoll_events(uint32_t events);

private:
    SessionsById  m_sessions; /*< A mapping of session_id->MXS_SESSION. The map
                               *  should contain sessions exclusive to this
                               *  worker and not e.g. listener sessions. For now,
                               *  it's up to the protocol to decide whether a new
                               *  session is added to the map. */
    Zombies       m_zombies;  /*< DCBs to be deleted. */
};

}
