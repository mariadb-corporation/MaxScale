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

#include <maxscale/ccdefs.hh>

#include <unordered_map>
#include <vector>
#include <mutex>
#include <type_traits>
#include <atomic>

#include <maxbase/atomic.hh>
#include <maxbase/semaphore.hh>
#include <maxbase/worker.hh>
#include <maxbase/stopwatch.hh>
#include <maxscale/poll.hh>
#include <maxscale/query_classifier.hh>
#include <maxscale/session.hh>

MXS_BEGIN_DECLS

// The worker ID of the "main" thread
#define MXS_RWORKER_MAIN -1

/**
 * Return the routing worker associated with the provided worker id.
 *
 * @param worker_id  A worker id. If MXS_RWORKER_MAIN is used, the
 *                   routing worker running in the main thread will
 *                   be returned.
 *
 * @return The corresponding routing worker instance, or NULL if the
 *         id does not correspond to a routing worker.
 */
MXB_WORKER* mxs_rworker_get(int worker_id);

/**
 * Return the current routing worker.
 *
 * @return A routing worker, or NULL if there is no current routing worker.
 */
MXB_WORKER* mxs_rworker_get_current();

/**
 * Return the id of the current routing worker.
 *
 * @return The id of the routing worker, or -1 if there is no current
 *         routing worker.
 */
int mxs_rworker_get_current_id();

/**
 * Broadcast a message to all routing workers.
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
size_t mxs_rworker_broadcast_message(uint32_t msg_id, intptr_t arg1, intptr_t arg2);

/**
 * Call a function on all workers
 *
 * A convenience function for executing simple tasks on all workers. The task
 * will be executed immediately on the current worker and thus recursive calls
 * into functions should not be done.
 *
 * @param cb Callback to call
 * @param data Data passed to the callback
 *
 * @return The number of messages posted; if less that ne number of workers
 *         then some postings failed.
 */
size_t mxs_rworker_broadcast(void (* cb)(void* data), void* data);

/**
 * Add a session to the current routing worker's session container. Currently
 * only required for some special commands e.g. "KILL <process_id>" to work.
 *
 * @param session Session to add.
 * @return true if successful, false if id already existed in map.
 */
bool mxs_rworker_register_session(MXS_SESSION* session);

/**
 * Remove a session from the current routing worker's session container. Does
 * not actually remove anything from an epoll-set or affect the session in any
 * way.
 *
 * @param id Which id to remove.
 * @return The removed session or NULL if not found.
 */
bool mxs_rworker_deregister_session(uint64_t id);

/**
 * Find a session in the current routing worker's session container.
 *
 * @param id Which id to find.
 * @return The found session or NULL if not found.
 */
MXS_SESSION* mxs_rworker_find_session(uint64_t id);

/**
 * Worker local storage
 */

/**
 * Initialize a globally unique data identifier
 *
 * The value returned by this function is used with the other data commands.
 * The value is a unique handle to thread-local storage.
 *
 * @return The data identifier usable for worker local data storage
 */
uint64_t mxs_rworker_create_key();

/**
 * Set local worker data on current worker
 *
 * @param key      Key acquired with create_data
 * @param data     Data to store
 * @param callback Callback used to delete the data, NULL if no deletion is
 *                 required. This function is called by mxs_rworker_delete_data
 *                 when the data is deleted.
 */
void mxs_rworker_set_data(uint64_t key, void* data, void (* callback)(void*));

/**
 * Get local data from current worker
 *
 * @param key    Key to use
 *
 * @return Data previously stored or NULL if no data was previously stored
 */
void* mxs_rworker_get_data(uint64_t key);

/**
 * Deletes local data from all workers
 *
 * The key must not be used again after deletion.
 *
 * @param key      Key to remove
 */
void mxs_rworker_delete_data(uint64_t key);

MXS_END_DECLS

namespace maxscale
{

class RoutingWorker : public mxb::Worker
                    , private MXB_POLL_DATA
{
    RoutingWorker(const RoutingWorker&) = delete;
    RoutingWorker& operator=(const RoutingWorker&) = delete;

public:
    enum
    {
        MAIN = -1
    };

    typedef Registry<MXS_SESSION> SessionsById;
    typedef std::vector<DCB*>     Zombies;

    typedef std::unordered_map<uint64_t, void*>           LocalData;
    typedef std::unordered_map<uint64_t, void (*)(void*)> DataDeleters;

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
    static bool add_shared_fd(int fd, uint32_t events, MXB_POLL_DATA* pData);

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
     * Starts all routing workers.
     *
     * @return True, if all workers could be started.
     */
    static bool start_workers();

    /**
     * Waits for all routing workers.
     */
    static void join_workers();

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
     * Posts a function to all workers for execution.
     *
     * @param pSem If non-NULL, will be posted once the task's `execute` return.
     * @param mode Execution mode
     *
     * @return How many workers the task was posted to.
     */
    static size_t broadcast(std::function<void ()> func, mxb::Semaphore* pSem, execute_mode_t mode);

    static size_t broadcast(std::function<void ()> func, enum execute_mode_t mode)
    {
        return broadcast(func, NULL, mode);
    }

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
        return mxb::atomic::add(&id_generator, 1, mxb::atomic::RELAXED);
    }

    /**
     * Set local data
     *
     * @param key  Key acquired with create_local_data
     * @param data Data to store
     */
    void set_data(uint64_t key, void* data, void (* callback)(void*))
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

    /**
     * To be called from the initial (parent) thread if the systemd watchdog is on.
     */
    static void set_watchdog_interval(uint64_t microseconds);

    class WatchdogWorkaround;
    friend WatchdogWorkaround;

    /**
     * @class WatchdogWorkaround
     *
     * RAII-class using which the systemd watchdog notification can be
     * handled during synchronous worker activity that causes the epoll
     * event handling to be stalled.
     *
     * The constructor turns on the workaround and the destructor
     * turns it off.
     */
    class WatchdogWorkaround
    {
        WatchdogWorkaround(const WatchdogWorkaround&);
        WatchdogWorkaround& operator=(const WatchdogWorkaround&);

    public:
        /**
         * Turns on the watchdog workaround for a specific worker.
         *
         * @param pWorker  The worker for which the systemd notification
         *                 should be arranged. Need not be the calling worker.
         */
        WatchdogWorkaround(RoutingWorker* pWorker)
            : m_pWorker(pWorker)
        {
            mxb_assert(pWorker);
            m_pWorker->start_watchdog_workaround();
        }

        /**
         * Turns on the watchdog workaround for the calling worker.
         */
        WatchdogWorkaround()
            : WatchdogWorkaround(RoutingWorker::get_current())
        {
        }

        /**
         * Turns off the watchdog workaround.
         */
        ~WatchdogWorkaround()
        {
            m_pWorker->stop_watchdog_workaround();
        }

    private:
        RoutingWorker* m_pWorker;
    };

private:
    class WatchdogNotifier;
    friend WatchdogNotifier;

    const int    m_id;              /*< The id of the worker. */
    SessionsById m_sessions;        /*< A mapping of session_id->MXS_SESSION. The map
                                     *  should contain sessions exclusive to this
                                     *  worker and not e.g. listener sessions. For now,
                                     *  it's up to the protocol to decide whether a new
                                     *  session is added to the map. */
    Zombies      m_zombies;         /*< DCBs to be deleted. */
    LocalData    m_local_data;      /*< Data local to this worker */
    DataDeleters m_data_deleters;   /*< Delete functions for the local data */

    RoutingWorker();
    virtual ~RoutingWorker();

    static RoutingWorker* create(int epoll_listener_fd);

    bool pre_run();     // override
    void post_run();    // override
    void epoll_tick();  // override

    void delete_zombies();
    void check_systemd_watchdog();
    void start_watchdog_workaround();
    void stop_watchdog_workaround();

    static uint32_t epoll_instance_handler(MXB_POLL_DATA* data, MXB_WORKER* worker, uint32_t events);
    uint32_t        handle_epoll_events(uint32_t events);

    static maxbase::Duration  s_watchdog_interval;    /*< Duration between notifications, if any. */
    static maxbase::TimePoint s_watchdog_next_check;  /*< Next time to notify systemd. */
    std::atomic<bool>         m_alive;                /*< Set to true in epoll_tick(), false on notification. */
    WatchdogNotifier*         m_pWatchdog_notifier;   /*< Watchdog notifier, if systemd enabled. */
};

using WatchdogWorkaround = RoutingWorker::WatchdogWorkaround;

// Data local to a routing worker
template<class T>
class rworker_local
{
public:

    rworker_local(const rworker_local&) = delete;
    rworker_local& operator=(const rworker_local&) = delete;

    // Default initialized
    rworker_local()
        : m_handle(mxs_rworker_create_key())
    {
    }

    // Copy-constructed
    rworker_local(const T& t)
        : m_handle(mxs_rworker_create_key())
        , m_value(t)
    {
    }

    ~rworker_local()
    {
        mxs_rworker_delete_data(m_handle);
    }

    // Converts to a T reference
    operator T&() const
    {
        return *get_local_value();
    }

    // Arrow operator
    T* operator->() const
    {
        return get_local_value();
    }

    // Dereference operator
    T& operator*()
    {
        return *get_local_value();
    }

    /**
     * Assign a value
     *
     * Sets the master value and triggers an update on all workers. The value is updated instantly
     * if the calling thread is a worker thread.
     *
     * @param t The new value to assign
     */
    void assign(const T& t)
    {
        std::unique_lock<std::mutex> guard(m_lock);
        m_value = t;
        guard.unlock();

        // Update the value on all workers
        mxs_rworker_broadcast(update_value, this);
    }

    /**
     * Get all local values
     *
     * Note: this method must be called from the main worker thread.
     *
     * @return A vector containing the individual values for each worker
     */
    std::vector<T> values() const
    {
        mxb_assert_message(RoutingWorker::get_current() == RoutingWorker::get(RoutingWorker::MAIN),
                           "this method must be called from the main worker thread");
        std::vector<T> rval;
        std::mutex lock;
        mxb::Semaphore sem;

        auto n = RoutingWorker::broadcast([&]() {
                                              std::lock_guard<std::mutex> guard(lock);
                                              rval.push_back(*get_local_value());
                                          },
                                          &sem,
                                          RoutingWorker::EXECUTE_AUTO);

        sem.wait_n(n);
        return std::move(rval);
    }

private:

    uint64_t                            m_handle;   // The handle to the worker local data
    typename std::remove_const<T>::type m_value;    // The master value, never used directly
    mutable std::mutex                  m_lock;     // Protects the master value

private:

    T* get_local_value() const
    {
        T* my_value = static_cast<T*>(mxs_rworker_get_data(m_handle));

        if (my_value == nullptr)
        {
            // First time we get the local value, allocate it from the master value
            std::unique_lock<std::mutex> guard(m_lock);
            my_value = new T(m_value);
            guard.unlock();

            mxs_rworker_set_data(m_handle, my_value, destroy_value);
        }

        mxb_assert(my_value);
        return my_value;
    }

    void update_local_value()
    {
        // As get_local_value can cause a lock to be taken, we need the pointer to our value before
        // we lock the master value for the updating of our value.
        T* my_value = get_local_value();

        std::lock_guard<std::mutex> guard(m_lock);
        *my_value = m_value;
    }

    static void update_value(void* data)
    {
        static_cast<rworker_local<T>*>(data)->update_local_value();
    }

    static void destroy_value(void* data)
    {
        delete static_cast<T*>(data);
    }
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

/**
 * @brief MaxScale worker watchdog
 *
 * If this function returns, then MaxScale is alive. If not,
 * then some thread is dead.
 */
void mxs_rworker_watchdog();
