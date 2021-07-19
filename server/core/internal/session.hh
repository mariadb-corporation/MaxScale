/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-07-19
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

#include <maxscale/buffer.hh>
#include <maxscale/session.hh>
#include <maxscale/utils.hh>
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

// Class that holds the session specific filter data
class SessionFilter
{
public:

    SessionFilter(const SFilterDef& f)
        : filter(f)
        , instance(filter->filter)
        , session(nullptr)
    {
    }

    SFilterDef          filter;
    MXS_FILTER*         instance;
    MXS_FILTER_SESSION* session;
    mxs::Upstream       up;
    mxs::Downstream     down;
};

class Session : public MXS_SESSION, public mxs::Component
{
public:
    class QueryInfo
    {
    public:
        QueryInfo(const std::shared_ptr<GWBUF>& sQuery);

        json_t* as_json() const;

        bool complete() const
        {
            return m_complete;
        }

        const std::shared_ptr<GWBUF>& query() const
        {
            return m_sQuery;
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

    private:
        std::shared_ptr<GWBUF>  m_sQuery;           /*< The packet, COM_QUERY *or* something else. */
        timespec                m_received;         /*< When was it received. */
        timespec                m_completed;        /*< When was it completed. */
        std::vector<ServerInfo> m_server_infos;     /*< When different servers responded. */
        bool                    m_complete = false; /*< Is this information complete? */
    };

    using FilterList = std::vector<SessionFilter>;
    using DCBSet = std::unordered_set<DCB*>;
    using BackendConnectionVector = std::vector<mxs::BackendConnection*>;

    Session(std::shared_ptr<mxs::ListenerSessionData> listener_data,
            const std::string& host);
    ~Session();

    bool start();
    void close();

    // Links a client DCB to a session
    void set_client_dcb(ClientDCB* dcb);

    const FilterList& get_filters() const
    {
        return m_filters;
    }

    bool  add_variable(const char* name, session_variable_handler_t handler, void* context);
    char* set_variable_value(const char* name_begin,
                             const char* name_end,
                             const char* value_begin,
                             const char* value_end);
    bool remove_variable(const char* name, void** context);
    void retain_statement(GWBUF* pBuffer);
    void dump_statements() const;
    void book_server_response(SERVER* pServer, bool final_response);
    void book_last_as_complete();
    void reset_server_bookkeeping();
    void append_session_log(std::string);
    void dump_session_log();

    json_t* queries_as_json() const;
    json_t* log_as_json() const;

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

    BackendDCB* create_backend_connection(Server* server, BackendDCB::Manager* manager,
                                          mxs::Component* upstream);

    const BackendConnectionVector& backend_connections() const
    {
        return m_backends_conns;
    }

    // Implementation of mxs::Component
    int32_t routeQuery(GWBUF* buffer) override;
    int32_t clientReply(GWBUF* buffer, mxs::ReplyRoute& down, const mxs::Reply& reply) override;
    bool    handleError(mxs::ErrorType type, GWBUF* error, mxs::Endpoint* down,
                        const mxs::Reply& reply) override;

    mxs::ClientConnection*       client_connection() override;
    const mxs::ClientConnection* client_connection() const override;
    mxs::ListenerSessionData*    listener_data() override;

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

protected:
    std::unique_ptr<mxs::Endpoint> m_down;

private:
    void adjust_io_activity(time_t now) const;
    void add_backend_conn(mxs::BackendConnection* conn);
    void remove_backend_conn(mxs::BackendConnection* conn);
    void parse_and_set_trx_state(const mxs::Reply& reply);

    // Delivers a provided response to the upstream filter that should receive it
    void deliver_response();

    struct SESSION_VARIABLE
    {
        session_variable_handler_t handler;
        void*                      context;
    };

    using SessionVarsByName = std::unordered_map<std::string, SESSION_VARIABLE>;
    using QueryInfos = std::deque<QueryInfo>;
    using Log = std::deque<std::string>;

    FilterList        m_filters;
    SessionVarsByName m_variables;
    QueryInfos        m_last_queries;           /*< The N last queries by the client */
    int               m_current_query {-1};     /*< The index of the current query */
    uint32_t          m_retain_last_statements; /*< How many statements be retained */
    Log               m_log;                    /*< Session specific in-memory log */
    int64_t           m_ttl = 0;                /*< How many seconds the session has until it is killed  */
    int64_t           m_ttl_start = 0;          /*< The clock tick when TTL was assigned */

    BackendConnectionVector m_backends_conns;   /*< Backend connections, in creation order */
    mxs::ClientConnection*  m_client_conn {nullptr};

    // Various listener-specific data the session needs. Ownership shared with the listener that
    // created this session.
    std::shared_ptr<mxs::ListenerSessionData> m_listener_data;
    std::shared_ptr<mxs::ProtocolModule>      m_protocol;

    static const int N_LOAD = 30;   // Last 30 seconds.

    mutable std::array<int, N_LOAD> m_io_activity {};
    time_t                          m_last_io_activity {0};
};
