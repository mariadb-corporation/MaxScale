/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-01-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/ccdefs.hh>

#include <deque>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <maxbase/window.hh>
#include <maxscale/buffer.hh>
#include <maxscale/session.hh>
#include <maxscale/target.hh>

#include "filter.hh"
#include "service.hh"

// The following may be called from a debugger session so use C-linkage to preserve names.
extern "C" {

void printAllSessions();
void dprintAllSessions(DCB*);
void dprintSession(DCB*, MXS_SESSION*);
void dListSessions(DCB*);
}

void printSession(MXS_SESSION*);
class Server;
namespace maxscale
{
class Listener;
}

// Class that holds the session specific filter data
class SessionFilter
{
public:

    SessionFilter(const SFilterDef& f)
        : filter(f)
        , instance(filter->instance())
        , session(nullptr)
    {
    }

    SFilterDef                          filter;
    mxs::Filter*                        instance;
    std::unique_ptr<mxs::FilterSession> session;
    mxs::Routable*                      up;
    mxs::Routable*                      down;
};

class Session : public MXS_SESSION, public mxs::Component
{
public:
    class QueryInfo
    {
    public:
        explicit QueryInfo(GWBUF query);

        json_t* as_json(const mxs::Parser::Helper& helper) const;

        bool complete() const
        {
            return m_complete;
        }

        const GWBUF& query() const
        {
            return m_query;
        }

        timespec time_completed() const
        {
            return m_completed;
        }

        void book_server_response(SERVER* pServer, bool final_response);
        void book_as_complete();
        void reset_server_bookkeeping();

        struct ServerInfo
        {
            SERVER*  pServer;
            timespec processed;
        };

        size_t static_size() const
        {
            return sizeof(*this);
        }

        size_t varying_size() const
        {
            size_t rv = 0;

            rv += m_query.varying_size();
            rv += m_server_infos.capacity() * sizeof(ServerInfo);

            return rv;
        }

        size_t runtime_size() const
        {
            return static_size() + varying_size();
        }

    private:
        GWBUF                   m_query;            /*< The packet, a query *or* something else. */
        timespec                m_received;         /*< When was it received. */
        timespec                m_completed;        /*< When was it completed. */
        std::vector<ServerInfo> m_server_infos;     /*< When different servers responded. */
        bool                    m_complete = false; /*< Is this information complete? */
    };

    using FilterList = std::vector<SessionFilter>;

    Session(std::shared_ptr<const mxs::ListenerData> listener_data,
            std::shared_ptr<const mxs::ConnectionMetadata> metadata,
            SERVICE* service, const std::string& host);
    ~Session();

    bool start() override;
    void close() override;

    /**
     * Suspends the session. A suspended session does not process any events.
     * It is permissible to suspend an already suspended session.
     *
     * @note The session will be suspended immediately if it is idle and
     *       no transaction is in process. Otherwise it will be suspended
     *       when it has become idle and no transaction is in process.
     *
     * @return True, if the session is no longer processing any events, i.e.
     *         it could be suspended immediately or had by now become
     *         suspended due to an earlier call to @c suspend() that did not
     *         result in an immediate suspension.
     */
    bool suspend();

    /**
     * Resumes the session. If the session was suspended, it will again start
     * processing events. If the session was still processing events because it
     * was not idle or in a transaction when it was suspended, it will simply
     * continue processing events. It is permissible to resume a session that
     * had earlier not been suspended.
     *
     * @return True, if the session earlier was not processing events, but
     *         now is.
     */
    bool resume();

    /**
     * @return True, if the session has been suspended but is still processing
     *         events, since it has not yet become idle or is still in a transaction.
     *
     * @note Either but not both of @c is_suspending() and @c is_suspended() may
     *       return true. Both may return false.
     */
    bool is_suspending() const
    {
        return m_suspend && (!is_idle() || is_in_trx());
    }

    /**
     * @return True, if the session has been suspended and is not processing events.
     *
     * @note Either but not both of @c is_suspending() and @c is_suspended() may
     *       return true. Both may return false.
     */
    bool is_suspended() const
    {
        return m_suspend && (is_idle() && !is_in_trx());
    }

    /**
     * Flags the session for a restart
     *
     * Causes the router and filter sessions to be recreated without the client connection being affected.
     * The actual restart is done when the next routeQuery call is made.
     *
     * The restarting can fail if the new Endpoint cannot be opened. In this case the restart is not
     * automatically attempted again and must be triggered again manually.
     *
     * @return True if the restarting was initialized
     */
    bool restart();

    const FilterList& get_filters() const
    {
        return m_filters;
    }

    bool        add_variable(const char* name, session_variable_handler_t handler, void* context) override;
    std::string set_variable_value(const char* name_begin, const char* name_end,
                                   const char* value_begin, const char* value_end) override;
    bool remove_variable(const char* name, void** context) override;
    void retain_statement(const GWBUF& buffer) override;
    void dump_statements() const override;
    void book_server_response(mxs::Target* pTarget, bool final_response) override;
    void reset_server_bookkeeping() override;
    void append_session_log(struct timeval tv, std::string_view msg) override;
    void dump_session_log() override;

    json_t* as_json_resource(const char* host, bool rdns) const;
    json_t* queries_as_json() const;
    json_t* log_as_json() const;

    // Update the session from JSON
    bool update(json_t* json);
    void update_log_level(json_t* param, const char* key, int level);

    bool is_idle() const override;

    /**
     * Link a session to a backend connection.
     *
     * @param conn The backend connection to link
     */
    void link_backend_connection(mxs::BackendConnection* conn);

    /**
     * Unlink a session from a backend connection.
     *
     * @param conn The backend connection to unlink
     */
    void unlink_backend_connection(mxs::BackendConnection* conn);

    mxs::BackendConnection*
    create_backend_connection(Server* server, BackendDCB::Manager* manager, mxs::Component* upstream);

    const BackendConnectionVector& backend_connections() const override
    {
        return m_backend_conns;
    }

    // Implementation of mxs::Component
    bool routeQuery(GWBUF&& buffer) override;
    bool clientReply(GWBUF&& buffer, const mxs::ReplyRoute& down, const mxs::Reply& reply) override;
    bool handleError(mxs::ErrorType type, const std::string& error,
                     mxs::Endpoint* down, const mxs::Reply& reply) override;

    mxs::ClientConnection*       client_connection() override;
    const mxs::ClientConnection* client_connection() const override;

    const mxs::ListenerData*   listener_data() const override final;
    const mxs::ProtocolModule* protocol() const override final;

    void set_client_connection(mxs::ClientConnection* client_conn) override;

    /**
     * Perform periodic tasks
     *
     * This should only be called by a RoutingWorker.
     *
     * @param idle Number of seconds the session has been idle
     */
    void tick(int64_t idle);

    /**
     * Record that I/O activity was performed for the session.
     */
    void book_io_activity();

    /**
     * The I/O activity of the session.
     *
     * @return  The number of I/O events handled during the last 30 seconds.
     */
    int io_activity() const;

    /**
     * Can the session be moved to another thread. The function should be called from the thread
     * currently running the session to get up-to-date results. Any event processing on
     * the session may change the movable-status.
     *
     * @return True if session can be moved
     */
    bool is_movable() const;

    /**
     * With this function, a session can be moved from the worker it is
     * currently handled by, to another.
     *
     * @note This function must be called from the worker that currently
     *       is handling the session.
     * @note When a session is moved, there must be *no* events still to
     *       be delivered to any of the dcbs of the session. This is most
     *       easily handled by performing the move from the epoll_tick()
     *       function.
     *
     * @param worker  The worker the session should be moved to.
     *
     * @return True, if the move could be initiated, false otherwise.
     */
    bool move_to(mxs::RoutingWorker* worker);

    /**
     * Set session time-to-live value
     *
     * Setting a positive value causes the session to be closed after that many seconds. This is essentially a
     * delayed fake hangup event.
     */
    void set_ttl(int64_t ttl);

    /**
     * Execute a function for each session
     *
     * The function is executed concurrently on all routing workers.
     *
     * @param func The function executed for each session
     */
    static void foreach(std::function<void(Session*)> func);

    /**
     * Stop all sessions to a particular service
     *
     * @param service Stop all sessions that are connected to this service
     */
    static void kill_all(SERVICE* service);

    /**
     * Stop all sessions to a particular listener
     *
     * @param listener Stop all sessions that connected via this listener
     */
    static void kill_all(mxs::Listener* listener);

    void notify_userdata_change() override;

    bool can_pool_backends() const override;
    void set_can_pool_backends(bool value) override;
    bool idle_pooling_enabled() const override;

    std::chrono::seconds multiplex_timeout() const;

    mxb::Json get_memory_statistics() const override final;

    size_t static_size() const override final;

    size_t varying_size() const override final;

    const mxs::ConnectionMetadata& connection_metadata() const override final
    {
        mxb_assert(m_metadata);
        return *m_metadata;
    }

    bool is_enabled() const
    {
        return m_enabled;
    }

    mxs::Component* parent() const override
    {
        return nullptr;
    }

protected:
    std::shared_ptr<mxs::Endpoint> m_down;

private:
    void enable_events();
    void disable_events();

    size_t get_memory_statistics(size_t* connection_buffers,
                                 size_t* last_queries,
                                 size_t* variables) const;

    void adjust_io_activity(time_t now) const;
    void add_backend_conn(mxs::BackendConnection* conn);
    void remove_backend_conn(mxs::BackendConnection* conn);

    void add_userdata_subscriber(MXS_SESSION::EventSubscriber* obj) override;
    void remove_userdata_subscriber(MXS_SESSION::EventSubscriber* obj) override;
    bool pool_backends_cb(mxb::Worker::Callable::Action action);

    void setup_routing_chain();
    void do_restart();

    class SessionRoutable : public mxs::Routable
    {
    public:
        SessionRoutable(Session* session)
            : m_session(session)
        {
        }

        bool routeQuery(GWBUF&& packet) override
        {
            return m_session->m_down->routeQuery(std::move(packet));
        }

        bool clientReply(GWBUF&& packet, const mxs::ReplyRoute& down, const mxs::Reply& reply) override
        {
            return m_session->m_client_conn->clientReply(
                std::move(packet), const_cast<mxs::ReplyRoute&>(down), reply);
        }

    private:
        Session* m_session;
    };

    friend class SessionRoutable;

    struct SESSION_VARIABLE
    {
        session_variable_handler_t handler;
        void*                      context;
    };

    using SessionVarsByName = std::unordered_map<std::string, SESSION_VARIABLE>;
    using QueryInfos = std::deque<QueryInfo>;
    using Log = mxb::Window<std::pair<struct timeval, std::string>>;

    MXB_AT_DEBUG(bool m_routing {false});

    const time_t                 m_connected;   // System time when the session was started
    const mxb::Clock::time_point m_started;     // Steady clock time for measuring durations

    FilterList        m_filters;
    SessionVarsByName m_variables;
    QueryInfos        m_last_queries;           /*< The N last queries by the client */
    int               m_current_query {-1};     /*< The index of the current query */
    uint32_t          m_retain_last_statements; /*< How many statements be retained */
    Log               m_log {0};                /*< Session specific in-memory log */
    bool              m_dumping_log {false};    /*< If true, the session is dumping the log */
    int64_t           m_ttl = 0;                /*< How many seconds the session has until it is killed  */
    int64_t           m_ttl_start = 0;          /*< The clock tick when TTL was assigned */

    /**< Pre-emptive pooling time from service. Locked at session begin. */
    std::chrono::milliseconds m_pooling_time;
    /**< Multiplex timeout from service. Locked at session begin. */
    std::chrono::seconds m_multiplex_timeout;

    /**
     * Delayed call id for idle connection pooling. Needs to be cancelled on dtor or session move.
     * If more such timers are added, add also functions to cancel/move them all.
     */
    mxb::Worker::DCId m_idle_pool_call_id {mxb::Worker::NO_CALL};

    /**
     * Is session in a state where backend connections can be donated to pool and reattached to session?
     * Updated by protocol code. */
    bool m_can_pool_backends {false};

    SessionRoutable m_routable;
    mxs::Routable*  m_head;
    mxs::Routable*  m_tail;

    bool       m_restart = false;
    bool       m_rebuild_chain = false;
    FilterList m_pending_filters;

    /*< Objects listening for userdata change events */
    std::set<MXS_SESSION::EventSubscriber*> m_event_subscribers;

    BackendConnectionVector m_backend_conns;   /*< Backend connections, in creation order */
    mxs::ClientConnection*  m_client_conn {nullptr};

    // Various listener-specific data the session needs. Ownership shared with the listener that
    // created this session.
    std::shared_ptr<const mxs::ListenerData>  m_listener_data;
    std::shared_ptr<const mxs::ConnectionMetadata> m_metadata;

    static const int N_LOAD = 30;   // Last 30 seconds.

    mutable std::array<int, N_LOAD> m_io_activity {};
    time_t                          m_last_io_activity {0};
    bool                            m_enabled {true};
    bool                            m_suspend {false};
};

/**
 * @brief Get a session reference by ID
 *
 * This creates an additional reference to a session whose unique ID matches @c id.
 *
 * @param id Unique session ID
 * @return Reference to a Session or NULL if the session was not found
 *
 * @note The caller must free the session reference by calling session_put_ref
 */
Session* session_get_by_id(uint64_t id);
