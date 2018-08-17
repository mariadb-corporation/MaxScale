#pragma once
/*
 * Copyright (c) 2016 MariaDB Corporation Ab
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

#include <maxscale/ccdefs.hh>

#include <unordered_map>

#include <maxscale/poll.h>
#include <maxscale/query_classifier.h>
#include <maxscale/routingworker.h>
#include <maxscale/worker.hh>

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

    typedef std::unordered_map<uint64_t, void*>          LocalData;
    typedef std::unordered_map<uint64_t, void(*)(void*)> DataDeleters;

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
     * Returns the id of the routing worker
     *
     * @return The id of the routing worker.
     */
    int id() const
    {
        return m_id;
    }

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

    /**
     * Posts a task to all workers for execution.
     *
     * @param pTask  The task to be executed.
     * @param pSem   If non-NULL, will be posted once per worker when the task's
     *               `execute` return.
     *
     * @return How many workers the task was posted to.
     *
     * @attention The very same task will be posted to all workers. The task
     *            should either not have any sharable data or then it should
     *            have data specific to each worker that can be accessed
     *            without locks.
     *
     * @attention The task will be posted to each routing worker using the
     *            EXECUTE_AUTO execution mode. That is, if the calling thread
     *            is that of a routing worker, then the task will be executed
     *            directly without going through the message loop of the worker,
     *            otherwise the task is delivered via the message loop.
     */
    static size_t broadcast(Task* pTask, mxb::Semaphore* pSem = NULL);

    /**
     * Posts a task to all workers for execution.
     *
     * @param pTask  The task to be executed.
     *
     * @return How many workers the task was posted to.
     *
     * @attention The very same task will be posted to all workers. The task
     *            should either not have any sharable data or then it should
     *            have data specific to each worker that can be accessed
     *            without locks.
     *
     * @attention Once the task has been executed by all workers, it will
     *            be deleted.
     *
     * @attention The task will be posted to each routing worker using the
     *            EXECUTE_AUTO execution mode. That is, if the calling thread
     *            is that of a routing worker, then the task will be executed
     *            directly without going through the message loop of the worker,
     *            otherwise the task is delivered via the message loop.
     */
    static size_t broadcast(std::unique_ptr<DisposableTask> sTask);

    /**
     * Executes a task on all workers in serial mode (the task is executed
     * on at most one worker thread at a time). When the function returns
     * the task has been executed on all workers.
     *
     * @param task  The task to be executed.
     *
     * @return How many workers the task was posted to.
     *
     * @warning This function is extremely inefficient and will be slow compared
     * to the other functions. Only use this function when printing thread-specific
     * data to stdout.
     *
     * @attention The task will be posted to each routing worker using the
     *            EXECUTE_AUTO execution mode. That is, if the calling thread
     *            is that of a routing worker, then the task will be executed
     *            directly without going through the message loop of the worker,
     *            otherwise the task is delivered via the message loop.
     */
    static size_t execute_serially(Task& task);

    /**
     * Executes a task on all workers concurrently and waits until all workers
     * are done. That is, when the function returns the task has been executed
     * by all workers.
     *
     * @param task  The task to be executed.
     *
     * @return How many workers the task was posted to.
     *
     * @attention The task will be posted to each routing worker using the
     *            EXECUTE_AUTO execution mode. That is, if the calling thread
     *            is that of a routing worker, then the task will be executed
     *            directly without going through the message loop of the worker,
     *            otherwise the task is delivered via the message loop.
     */
    static size_t execute_concurrently(Task& task);

    /**
     * Broadcast a message to all worker.
     *
     * @param msg_id  The message id.
     * @param arg1    Message specific first argument.
     * @param arg2    Message specific second argument.
     *
     * @return The number of messages posted; if less that ne number of workers
     *         then some postings failed.
     *
     * @attention The return value tells *only* whether message could be posted,
     *            *not* that it has reached the worker.
     *
     * @attentsion Exactly the same arguments are passed to all workers. Take that
     *             into account if the passed data must be freed.
     *
     * @attention This function is signal safe.
     */
    static size_t broadcast_message(uint32_t msg_id, intptr_t arg1, intptr_t arg2);

    /**
     * Initate shutdown of all workers.
     *
     * @attention A call to this function will only initiate the shutdowm,
     *            the workers will not have shut down when the function returns.
     *
     * @attention This function is signal safe.
     */
    static void shutdown_all();

    /**
     * Returns statistics for all workers.
     *
     * @return Combined statistics.
     *
     * @attentions The statistics may no longer be accurate by the time it has
     *             been returned. The returned values may also not represent a
     *             100% consistent set.
     */
    static STATISTICS get_statistics();

    /**
     * Return a specific combined statistic value.
     *
     * @param what  What to return.
     *
     * @return The corresponding value.
     */
    static int64_t get_one_statistic(POLL_STAT what);

    /**
     * Get next worker
     *
     * @return The worker where work should be assigned
     */
    static RoutingWorker* pick_worker();

    /**
     * Worker local storage
     */

    /**
     * Initialize a globally unique data identifier
     *
     * @return The data identifier usable for worker local data storage
     */
    static uint64_t create_key()
    {
        static uint64_t id_generator = 0;
        return atomic_add_uint64(&id_generator, 1);
    }

    /**
     * Set local data
     *
     * @param key  Key acquired with create_local_data
     * @param data Data to store
     */
    void set_data(uint64_t key, void* data, void (*callback)(void*))
    {
        if (callback)
        {
            m_data_deleters[key] = callback;
        }

        m_local_data[key] = data;
    }

    /**
     * Get local data
     *
     * @param key Key to use
     *
     * @return Data previously stored
     */
    void* get_data(uint64_t key)
    {
        auto it = m_local_data.find(key);
        return it != m_local_data.end() ? it->second : NULL;
    }

    /**
     * Deletes local data
     *
     * If a callback was passed when the data was set, it will be called.
     *
     * @param key Key to remove
     */
    void delete_data(uint64_t key)
    {
        auto data = m_local_data.find(key);

        if (data != m_local_data.end())
        {
            auto deleter = m_data_deleters.find(key);

            if (deleter != m_data_deleters.end())
            {
                deleter->second(data->second);
                m_data_deleters.erase(deleter);
            }

            m_local_data.erase(data);
        }
    }

    /**
     * Provides QC statistics of one workers
     *
     * @param id[in]       Id of worker.
     * @param pStats[out]  The QC statistics of that worker.
     *
     * return True, if @c id referred to a worker, false otherwise.
     */
    static bool get_qc_stats(int id, QC_CACHE_STATS* pStats);

    /**
     * Provides QC statistics of all workers
     *
     * @param all_stats  Vector that on return will contain the statistics of all workers.
     */
    static void get_qc_stats(std::vector<QC_CACHE_STATS>& all_stats);

    /**
     * Provides QC statistics of all workers as a Json object for use in the REST-API.
     */
    static std::unique_ptr<json_t> get_qc_stats_as_json(const char* zHost);

    /**
     * Provides QC statistics of one worker as a Json object for use in the REST-API.
     *
     * @param zHost  The name of the MaxScale host.
     * @param id     An id of a worker.
     *
     * @return A json object if @c id refers to a worker, NULL otherwise.
     */
    static std::unique_ptr<json_t> get_qc_stats_as_json(const char* zHost, int id);

private:
    const int    m_id;       /*< The id of the worker. */
    SessionsById m_sessions; /*< A mapping of session_id->MXS_SESSION. The map
                              *  should contain sessions exclusive to this
                              *  worker and not e.g. listener sessions. For now,
                              *  it's up to the protocol to decide whether a new
                              *  session is added to the map. */
    Zombies      m_zombies;  /*< DCBs to be deleted. */
    LocalData    m_local_data; /*< Data local to this worker */
    DataDeleters m_data_deleters; /*< Delete functions for the local data */

    RoutingWorker();
    virtual ~RoutingWorker();

    static RoutingWorker* create(int epoll_listener_fd);

    bool pre_run();    // override
    void post_run();   // override
    void epoll_tick(); // override

    void delete_zombies();

    static uint32_t epoll_instance_handler(struct mxs_poll_data* data, void* worker, uint32_t events);
    uint32_t handle_epoll_events(uint32_t events);
};

}

/**
 * @brief Convert a routing worker to JSON format
 *
 * @param host Hostname of this server
 * @param id   ID of the worker
 *
 * @return JSON resource representing the worker
 */
json_t* mxs_rworker_to_json(const char* host, int id);

/**
 * Convert routing workers into JSON format
 *
 * @param host Hostname of this server
 *
 * @return A JSON resource collection of workers
 *
 * @see mxs_json_resource()
 */
json_t* mxs_rworker_list_to_json(const char* host);
