/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-04-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/ccdefs.hh>

#include <atomic>
#include <deque>
#include <list>
#include <mutex>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <functional>

#include <maxbase/semaphore.hh>
#include <maxbase/stopwatch.hh>
#include <maxbase/watchedworker.hh>
#include <maxscale/dcb.hh>
#include <maxscale/indexedstorage.hh>
#include <maxscale/cachingparser.hh>
#include <maxscale/session.hh>

class ServerEndpoint;

namespace maxscale
{
class Listener;

class RoutingWorker : public mxb::WatchedWorker
                    , public BackendDCB::Manager
                    , private mxb::Pollable
{
    RoutingWorker(const RoutingWorker&) = delete;
    RoutingWorker& operator=(const RoutingWorker&) = delete;

public:
#ifdef SS_DEBUG
    // In debug mode a routing worker is deleted aggressively quickly
    // in the hope that it will bring to light any issues.
    static constexpr std::chrono::seconds TERMINATION_DELAY = 1s;
#else
    static constexpr std::chrono::seconds TERMINATION_DELAY = 5s;
#endif

    virtual ~RoutingWorker();

    class InfoTask;

    enum class State
    {
        ACTIVE,   /* Listening and/or routing. */
        DRAINING, /* Routing; /deactivated/ and once all sessions have ended => DORMANT. */
        DORMANT   /* Neither listening, nor routing. If /activated/ => ACTIVE. */
    };

    class MemoryUsage
    {
    public:
        MemoryUsage()
            : query_classifier(0)
            , zombies(0)
            , sessions(0)
            , total(0)
        {
        }

        MemoryUsage& operator += (const MemoryUsage& rhs)
        {
            this->query_classifier += rhs.query_classifier;
            this->zombies += rhs.zombies;
            this->sessions += rhs.sessions;
            this->total += rhs.total;
            return *this;
        }

        json_t* to_json() const;

        int64_t query_classifier;
        int64_t zombies;
        int64_t sessions;
        int64_t total;
    };

    typedef mxs::Registry<MXS_SESSION> SessionsById;
    typedef std::vector<DCB*>          Zombies;

    typedef std::vector<void*>           LocalData;
    typedef std::vector<void (*)(void*)> DataDeleters;

    /**
     * Class to be derived from, in case there is data to be
     * initialized separately for each worker.
     */
    class Data
    {
    public:
        Data(const Data&) = delete;
        Data& operator=(const Data&) = delete;

        virtual ~Data();

        /**
         * Called when the data should be initialized for a worker.
         * The call takes place in the thread context of the worker.
         *
         * @param pWorker  The worker for which @c Data should be initialized,
         *                 and in whose thread-context the call is made.
         */
        virtual void init_for(RoutingWorker* pWorker) = 0;

        /**
         * Called when the data should be finalized for a worker.
         * The call takes place in the thread context of the worker.
         *
         * @param pWorker  The worker for which @c Data should be finalized,
         *                 and in whose thread-context the call is made.
         */
        virtual void finish_for(RoutingWorker*) = 0;

    protected:
        Data();

        /**
         * Peform worker initialization.
         *
         * This call will, if the routing workers are already running, cause
         * @c init_for() to be called for each worker. If the routing workers
         * are not yet running, this call is a nop as in that case they will
         * be initialized when started.
         *
         * @note This must be called by any class derived from Data and must
         *       be called from MainWorker.
         */
        void initialize_workers();
    };

    /**
     * Initialize the routing worker mechanism.
     *
     * @param pNotifier  The watchdog notifier. Must remain alive for the
     *                   lifetime of the routing worker.
     *
     * @return True if the initialization succeeded, false otherwise.
     */
    static bool init(mxb::WatchdogNotifier* pNotifier);

    /**
     * Finalize the worker mechanism.
     *
     * To be called once at process shutdown. This will cause all workers
     * to be destroyed. When the function is called, no worker should be
     * running anymore.
     */
    static void finish();

    /**
     * Adjust number of routing threads.
     *
     * @param nCount  The number of configured routing workers.
     *
     * @return True, if the number could be adjusted, false, otherwise.
     */
    static bool adjust_threads(int nCount);

    /**
     * @return The number of active routing workers; less than or equal to created.
     */
    static int nRunning();

    /**
     * Add a Listener to the routing workers.
     *
     * @param pListener  The listener to be added.
     *
     * @return True, if the descriptor could be added, false otherwise.
     */
    static bool add_listener(Listener* pListener);

    /**
     * Remove a Listener from the routing workers.
     *
     * @param pListener  The lister to be removed.
     *
     * @return True on success, false on failure.
     */
    static bool remove_listener(Listener* pListener);

    const char* name() const override
    {
        return m_name.c_str();
    }

    /**
     * Return a reference to the session registry of this worker.
     *
     * @return Session registry.
     */
    SessionsById& session_registry();

    const SessionsById& session_registry() const;

    State state() const
    {
        return m_state.load(std::memory_order_relaxed);
    };

    bool is_active() const
    {
        return state() == State::ACTIVE;
    }

    bool is_draining() const
    {
        return state() == State::DRAINING;
    }

    bool is_dormant() const
    {
        return state() == State::DORMANT;
    }

    bool is_listening() const
    {
        return m_listening.load(std::memory_order_relaxed);
    }

    bool is_routing() const
    {
        return m_routing.load(std::memory_order_relaxed);
    }

    /**
     * Add a session to the current routing worker's session container.
     *
     * @param ses Session to add.
     */
    void register_session(MXS_SESSION* ses);

    /**
     * Remove a session from the current routing worker's session container.
     *
     * @param id Which id to remove
     */
    void deregister_session(uint64_t session_id);

    /**
     * Return the routing worker associated with the current thread.
     *
     * @return The worker instance, or NULL if the current thread does not have a routing worker.
     */
    static RoutingWorker* get_current();

    /**
     * Return the index of the routing worker. The index will be >= 0 and
     * < #routing threads.
     *
     * @return The index of the routing worker.
     */
    int index() const;

    /**
     * Get routing worker by index.
     *
     * @param index  The index of the routing worker.
     * @return The corresponding routing worker.
     */
    static RoutingWorker* get_by_index(int index);

    /**
     * Get first routing worker. As there will always be at least one routing
     * worker, this function will return a worker if MaxScale has started.
     *
     * @return The first routing worker.
     */
    static RoutingWorker* get_first()
    {
        return get_by_index(0);
    }

    /**
     * Starts routing workers.
     *
     * @param nWorkers  How many to start.
     *
     * @return True, if all workers could be started.
     */
    static bool start_workers(int nWorkers);

    /**
     * Returns whether worker threads are running
     *
     * @return True if worker threads are running
     */
    static bool is_running();

    /**
     * Waits for all routing workers.
     */
    static void join_workers();

    /**
     * Check if all workers have finished shutting down
     */
    static bool shutdown_complete();

    /**
     * Posts a task to workers for execution.
     *
     * @param pTask  The task to be executed.
     * @param pSem   If non-NULL, will be posted once per worker when the task's
     *              `execute` return.
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
    static size_t broadcast(Task* pTask, mxb::Semaphore* pSem = nullptr);

    /**
     * Posts a task to workers for execution.
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
     * Posts a functor to workers for execution.
     *
     * @param func  The functor to be executed.
     * @param pSem  If non-NULL, will be posted once the task's `execute` return.
     * @param mode  Execution mode
     *
     * @return How many workers the task was posted to.
     */
    static size_t broadcast(const std::function<void ()>& func,
                            mxb::Semaphore* pSem,
                            execute_mode_t mode);

    static size_t broadcast(const std::function<void ()>& func, execute_mode_t mode)
    {
        return broadcast(func, nullptr, mode);
    }

    /**
     * Executes a task on workers in serial mode (the task is executed
     * on at most one worker thread at a time). When the function returns
     * the task has been executed on all workers.
     *
     * @param task/func  The task/func to be executed.
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
    static size_t execute_serially(const std::function<void()>& func);

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
    static size_t execute_concurrently(const std::function<void()>& func);

    /**
     * Find a session and execute a function with it if found
     *
     * @param id The session ID to find
     * @param fn The function that is executed if the session is found
     *
     * @return True if the session was found and the function was executed
     */
    static bool execute_for_session(uint64_t id, std::function<void(MXS_SESSION*)> fn);

    /**
     * Broadcast a message to workers.
     *
     * @param msg_id    The message id.
     * @param arg1      Message specific first argument.
     * @param arg2      Message specific second argument.
     * @param nWorkers  ALL, RUNNING, CONFIGURED or a specific number of workers.
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
     * Returns statistics for all workers.
     *
     * @param nWorkers  ALL, RUNNING, CONFIGURED or a specific number of workers.
     *
     * @return Combined statistics.
     *
     * @attentions The statistics may no longer be accurate by the time it has
     *             been returned. The returned values may also not represent a
     *             100% consistent set.
     */
    static Statistics get_statistics();

    /**
     * Get next worker
     *
     * @return The worker where work should be assigned
     */
    static RoutingWorker* pick_worker();

    /**
     * Provides QC statistics of one workers
     *
     * @param index[in]    Index of routing worker.
     * @param pStats[out]  The QC statistics of that worker.
     *
     * return True, if @c index referred to a worker, false otherwise.
     */
    static bool get_qc_stats_by_index(int index, CachingParser::Stats* pStats);

    /**
     * Provides QC statistics of all workers
     *
     * @param nWorkers   ALL, RUNNING, CONFIGURED or a specific number of workers.
     * @param all_stats  Vector that on return will contain the statistics of all workers.
     */
    static void get_qc_stats(std::vector<CachingParser::Stats>& all_stats);

    /**
     * Provides QC statistics of all workers as a Json object for use in the REST-API.
     *
     * @param nWorkers  ALL, RUNNING, CONFIGURED or a specific number of workers.
     *
     * @return JSON object containing statistics.
     */
    static std::unique_ptr<json_t> get_qc_stats_as_json(const char* zHost);

    /**
     * Provides QC statistics of one worker as a Json object for use in the REST-API.
     *
     * @param zHost  The name of the MaxScale host.
     * @param index  The index of a worker.
     *
     * @return A json object if @c index refers to a worker, NULL otherwise.
     */
    static std::unique_ptr<json_t> get_qc_stats_as_json_by_index(const char* zHost, int index);

    using DCBs = std::unordered_set<DCB*>;
    /**
     * Access all DCBs of the routing worker.
     *
     * @attn Must only be called from worker thread.
     *
     * @return Unordered set of DCBs.
     */
    const DCBs& dcbs() const
    {
        mxb_assert(this == RoutingWorker::get_current());
        return m_dcbs;
    }

    struct ConnectionResult
    {
        bool                    conn_limit_reached {false};
        mxs::BackendConnection* conn {nullptr};
    };

    ConnectionResult
    get_backend_connection(SERVER* pSrv, MXS_SESSION* pSes, mxs::Component* pUpstream);

    mxs::BackendConnection* pool_get_connection(SERVER* pSrv, MXS_SESSION* pSes, mxs::Component* pUpstream);

    size_t pool_close_all_conns();
    void pool_close_all_conns_by_server(SERVER* pSrv);

    void add_conn_wait_entry(ServerEndpoint* ep);
    void erase_conn_wait_entry(ServerEndpoint* ep);
    void notify_connection_available(SERVER* server);
    bool conn_to_server_needed(const SERVER* srv) const;

    static void pool_set_size(const std::string& srvname, int64_t size);

    struct ConnectionPoolStats
    {
        size_t curr_size {0};   /**< Current pool size */
        size_t max_size {0};    /**< Maximum pool size achieved since startup */
        size_t times_empty {0}; /**< Times the current pool was empty */
        size_t times_found {0}; /**< Times when a connection was available from the pool */

        void add(const ConnectionPoolStats& rhs);
    };
    static ConnectionPoolStats pool_get_stats(const SERVER* pSrv);

    ConnectionPoolStats pool_stats(const SERVER* pSrv);

    /**
     * Register a function to be called every epoll_tick.
     */
    void register_epoll_tick_func(std::function<void(void)> func);

    /**
     * @return The indexed storage of this worker.
     */
    IndexedStorage& storage()
    {
        return m_storage;
    }

    const IndexedStorage& storage() const
    {
        return m_storage;
    }

    static void collect_worker_load(size_t count);

    static bool balance_workers();

    static bool balance_workers(int threshold);

    void rebalance(RoutingWorker* pTo, int nSessions = 1);

    static std::unique_ptr<json_t> memory_to_json(const char* zHost);

    MemoryUsage calculate_memory_usage() const;

    /**
     * Start the routingworker shutdown process
     */
    static void start_shutdown();

    /**
     * Set listen mode of worker.
     *
     * @note Only the listening mode of an active worker can be set.
     *       Attempting to set the listening mode of a draining or dormant
     *       worker is an error.
     * @note Assumed to be called from the REST-API.
     *
     * @param worker_index  The index of the worker.
     * @param enabled       If true, listening will be enabled, if false, disabled.
     *
     * @return True if the operation succeeded, otherwise false. No change is considered a success.
     */
    static bool set_listen_mode(int worker_index, bool enabled);

    /**
     * @return True, if a thread is being terminated.
     */
    static bool termination_in_process();

    struct SessionResult
    {
        size_t total { 0 };
        size_t affected { 0 };
    };

    /**
     * Restart sessions
     *
     * Causes the router and filter sessions to be recreated without the client
     * connection being affected. The actual restart is done when the next
     * routeQuery call is made.
     *
     * @return A @s SessionResult where
     *         - @c total tells the total amount of sessions, and
     *         - @c affected tells the number of sessions that could be restarted.
     */
    static SessionResult restart_sessions(std::string_view service);

    /**
     * Suspend sessions.
     *
     * A session will immediately be suspended, if it is idle and there is no
     * transaction in process. If the condition is not fulfilled, the session
     * will be suspended once it is. If the sessions have to be suspended, the
     * function should be called once and unless all sessions could immediately be
     * suspended, @c suspended_sessions should be called repeatedly (via the
     * event-loop) until the return value indicates that all sessions have been
     * suspended.
     *
     * @param service  The service whose sessions should be suspended.
     *
     * @return A @c SessionResult where
     *         - @c total tells the total amount of sessions, and
     *         - @c affected tells the number of sessions that currently
     *           are suspended (already were, or now became).
     *         That is, @c total - @c affected tells how many sessions
     *         have not yet been suspended.
     */
    static SessionResult suspend_sessions(std::string_view service);

    /**
     * Resume all sessions.
     *
     * @param service  The service whose sessions should be resumed.
     *
     * @return A @c SessionResult where
     *         - @c total tells the total number of sessions, and
     *         - @c affected tells the number of session that were resumed.
     *         That is, @c total - @c affected tells the number of sessions
     *         that were *not* suspended when the call was made.
     */
    static SessionResult resume_sessions(std::string_view service);

    /**
     * @param service  The service, whose suspended sessions are queried.
     *
     * @return A @c SessionResult where
     *         - @c total tells the total number of sessions, and
     *         - @c affected tells the number of sessions that currently are suspended.
     */
    static SessionResult suspended_sessions(std::string_view service);

private:
    // DCB::Manager
    void add(DCB* pDcb) override;
    void remove(DCB* pDcb) override;
    void destroy(DCB* pDcb) override;
    // BackendDCB::Manager
    bool move_to_conn_pool(BackendDCB* dcb) override;

    void evict_dcb(BackendDCB* pDcb);
    void close_pooled_dcb(BackendDCB* pDcb);

private:
    SessionResult restart_sessions(const SERVICE& service);
    SessionResult suspend_sessions(const SERVICE& Service);
    SessionResult resume_sessions(const SERVICE& Service);
    SessionResult suspended_sessions(const SERVICE& Service) const;

    void start_try_shutdown();
    bool try_shutdown_dcall();

    void set_state(State s)
    {
        mxb_assert((m_state == State::ACTIVE && (s == State::DRAINING || s == State::DORMANT))
                   || (m_state == State::DORMANT && s == State::ACTIVE)
                   || (m_state == State::DRAINING && (s == State::DORMANT || s == State::ACTIVE)));

        m_state.store(s, std::memory_order_relaxed);
    }

    static void register_data(Data* pData);
    static void deregister_data(Data* pData);

    struct Rebalance
    {
        RoutingWorker* pTo {nullptr};   /*< Worker to offload work to. */
        bool           perform = false;
        int            nSessions = 0;

        void set(RoutingWorker* pTo, int nSessions)
        {
            this->pTo = pTo;
            this->nSessions = nSessions;
            this->perform = true;
        }

        void reset()
        {
            pTo = nullptr;
            perform = false;
            nSessions = 0;
        }
    };

    RoutingWorker(int index, size_t rebalance_window);

    static std::unique_ptr<RoutingWorker> create(int index,
                                                 size_t rebalance_window,
                                                 const std::vector<std::shared_ptr<Listener>>& listeners);

    void set_listening(bool b)
    {
        m_listening.store(b, std::memory_order_relaxed);

        if (b)
        {
            // If worker is listening, then it is also routing. However, even
            // if it is not listening, it may still be routing.
            set_routing(true);
        }
    }

    void set_routing(bool b)
    {
        m_routing.store(b, std::memory_order_relaxed);
    }

    void init_datas();
    void finish_datas();

    static bool increase_workers(int nDelta);
    static bool decrease_workers(int nDelta);

    bool start_polling_on_shared_fd();
    bool stop_polling_on_shared_fd();

    static int activate_workers(int n);
    static bool create_workers(int n);

    bool start_listening(const std::vector<std::shared_ptr<Listener>>& listeners);
    bool stop_listening(const std::vector<std::shared_ptr<Listener>>& listeners);

    bool can_deactivate() const
    {
        return !is_listening() && m_sessions.empty();
    }

    uint8_t average_load() const
    {
        return m_average_load.value();
    }

    void update_average_load(size_t count);

    void terminate();
    static void terminate_last_if_dormant(bool first_attempt);

    void deactivate();
    bool activate(const std::vector<std::shared_ptr<Listener>>& listeners);

    void make_dcalls();
    void cancel_dcalls();

    bool pre_run() override;
    void post_run() override;
    void epoll_tick() override;

    void process_timeouts();
    void delete_zombies();
    void rebalance();
    void pool_close_expired();
    void activate_waiting_endpoints();
    void fail_timed_out_endpoints();

    int poll_fd() const override;
    uint32_t handle_poll_events(Worker* worker, uint32_t events, Pollable::Context context) override;

    class ConnPoolEntry
    {
    public:
        explicit ConnPoolEntry(mxs::BackendConnection* pConn);
        ~ConnPoolEntry();

        ConnPoolEntry(ConnPoolEntry&& rhs)
            : m_created(rhs.m_created)
            , m_pConn(rhs.release_conn())
        {
        }

        bool hanged_up() const
        {
            return m_pConn->dcb()->hanged_up();
        }

        time_t created() const
        {
            return m_created;
        }

        mxs::BackendConnection* conn() const
        {
            return m_pConn;
        }

        mxs::BackendConnection* release_conn()
        {
            mxs::BackendConnection* pConn = m_pConn;
            m_pConn = nullptr;
            return pConn;
        }

    private:
        time_t                  m_created;  /*< Time when entry was created. */
        mxs::BackendConnection* m_pConn {nullptr};
    };

    class DCBHandler : public DCB::Handler
    {
    public:
        DCBHandler(const DCBHandler&) = delete;
        DCBHandler& operator=(const DCBHandler&) = delete;

        DCBHandler(RoutingWorker* pOwner);

        void ready_for_reading(DCB* pDcb) override;
        void error(DCB* dcb, const char* errmsg) override;

    private:
        RoutingWorker& m_owner;
    };

    class ConnectionPool
    {
    public:
        ConnectionPool(mxs::RoutingWorker* pOwner, SERVER* pTarget_server, int global_capacity);
        ConnectionPool(ConnectionPool&& rhs);

        void remove_and_close(mxs::BackendConnection* pConn);
        void close_expired();
        size_t close_all();
        bool empty() const;
        bool has_space() const;
        void set_capacity(int global_capacity);

        ConnectionPoolStats stats() const;

        std::pair<uint64_t, mxs::BackendConnection*> get_connection(MXS_SESSION* pSession);

        void add_connection(mxs::BackendConnection* pConn);

    private:
        std::map<mxs::BackendConnection*, ConnPoolEntry> m_contents;

        mxs::RoutingWorker*         m_pOwner {nullptr};
        SERVER*                     m_pTarget_server {nullptr};
        int                         m_capacity {0}; // Capacity for this pool.
        mutable ConnectionPoolStats m_stats;
    };

    using ConnPoolGroup = std::map<const SERVER*, ConnectionPool>;
    using EndpointsBySrv = std::map<const SERVER*, std::deque<ServerEndpoint*>>;
    using TickFuncs = std::vector<std::function<void()>>;
    using Datas = std::vector<Data*>;

    static Datas       s_datas;     /*< Datas that need to be inited/finishe for each worker. */
    static std::mutex  s_datas_lock;

    int                m_index;     /*< Index of routing worker */
    std::string        m_name;
    std::atomic<State> m_state;     /*< State of routing worker */
    std::atomic<bool>  m_listening; /*< Is the routing worker listening. */
    std::atomic<bool>  m_routing;   /*< Is the routing worker routing. */
    Worker::Callable   m_callable;  /*< Context for own dcalls */
    SessionsById       m_sessions;  /*< A mapping of session_id->MXS_SESSION */
    Zombies            m_zombies;   /*< DCBs to be deleted. */
    IndexedStorage     m_storage;   /*< The storage of this worker. */
    DCBs               m_dcbs;      /*< DCBs managed by this worker. */
    Rebalance          m_rebalance;
    // Protects the connection pool. This is only contended when the REST API asks for statistics on the
    // connection pool and accessing it directly is significantly faster than waiting for the worker to finish
    // their current work and post the results.
    mutable std::mutex m_pool_lock;
    ConnPoolGroup      m_pool_group;     /**< Pooled connections for each server */
    // Has a ServerEndpoint activation round been scheduled already? Used to avoid adding multiple identical
    // delayed calls.
    bool               m_ep_activation_scheduled {false};
    EndpointsBySrv     m_eps_waiting_for_conn;      /**< ServerEndpoints waiting for a connection */
    DCBHandler         m_pool_handler;
    long               m_next_timeout_check {0};
    TickFuncs          m_epoll_tick_funcs;
    DCId               m_check_pool_dcid {0};
    DCId               m_activate_eps_dcid {0};
    DCId               m_timeout_eps_dcid {0};
    mxb::AverageN      m_average_load;
};

const char* to_string(RoutingWorker::State state);

}

/**
 * @brief Convert a routing worker to JSON format
 *
 * @param host  Hostname of this server
 * @param index The index number of the worker
 *
 * @return JSON resource representing the worker
 */
json_t* mxs_rworker_to_json(const char* host, int index);

/**
 * Convert routing workers into JSON format
 *
 * @param host  Hostname of this server
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
