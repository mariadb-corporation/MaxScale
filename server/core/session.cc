/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-02-16
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * @file session.c  - A representation of the session within the gateway.
 */
#include <maxscale/session.hh>

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <algorithm>
#include <string>
#include <sstream>

#include <maxbase/atomic.hh>
#include <maxbase/host.hh>
#include <maxbase/alloc.h>
#include <maxscale/clock.h>
#include <maxscale/cn_strings.hh>
#include <maxscale/dcb.hh>
#include <maxscale/housekeeper.h>
#include <maxscale/json_api.hh>
#include <maxscale/listener.hh>
#include <maxscale/modutil.hh>
#include <maxscale/poll.hh>
#include <maxscale/router.hh>
#include <maxscale/routingworker.hh>
#include <maxscale/service.hh>
#include <maxscale/utils.h>
#include <maxscale/protocol/mariadb/mysql.hh>

#include "internal/filter.hh"
#include "internal/session.hh"
#include "internal/server.hh"
#include "internal/service.hh"
#include "internal/listener.hh"

using std::string;
using std::stringstream;
using maxbase::Worker;
using namespace maxscale;

namespace
{

struct
{
    /* Global session id counter. Must be updated atomically. Value 0 is reserved for
     *  dummy/unused sessions.
     */
    uint64_t                  next_session_id;
    uint32_t                  retain_last_statements;
    session_dump_statements_t dump_statements;
    uint32_t                  session_trace;
} this_unit =
{
    1,
    0,
    SESSION_DUMP_STATEMENTS_NEVER,
    0
};
}

// static
const int Session::N_LOAD;

MXS_SESSION::MXS_SESSION(const std::string& host, SERVICE* service)
    : m_state(MXS_SESSION::State::CREATED)
    , m_id(session_get_next_id())
    , m_worker(mxs::RoutingWorker::get_current())
    , m_host(host)
    , client_dcb(nullptr)
    , stats{time(0)}
    , service(service)
    , refcount(1)
    , response{}
    , close_reason(SESSION_CLOSE_NONE)
    , load_active(false)
{
    mxs::RoutingWorker::get_current()->register_session(this);
}

MXS_SESSION::~MXS_SESSION()
{
    mxs::RoutingWorker::get_current()->deregister_session(m_id);
}

void MXS_SESSION::kill(GWBUF* error)
{
    if (!m_killed && (m_state == State::CREATED || m_state == State::STARTED))
    {
        mxb_assert(client_connection()->dcb()->is_open());
        m_killed = true;
        close_reason = SESSION_CLOSE_HANDLEERROR_FAILED;

        // Call the protocol kill function before changing the session state
        client_connection()->kill();

        if (m_state == State::STARTED)
        {
            // This signals the rest of the system that the session has started the shutdown procedure.
            // Currently it mainly affects debug assertions inside the protocol code.
            m_state = State::STOPPING;
        }

        if (error)
        {
            // Write the error to the client before closing the DCB
            client_connection()->write(error);
        }

        ClientDCB::close(client_dcb);
    }
}

MXS_SESSION::ProtocolData* MXS_SESSION::protocol_data() const
{
    return m_protocol_data.get();
}

void MXS_SESSION::set_protocol_data(std::unique_ptr<ProtocolData> new_data)
{
    m_protocol_data = std::move(new_data);
}

bool session_start(MXS_SESSION* ses)
{
    Session* session = static_cast<Session*>(ses);
    return session->start();
}

void Session::link_backend_connection(mxs::BackendConnection* conn)
{
    auto dcb = conn->dcb();
    mxb_assert(dcb->owner == m_client_conn->dcb()->owner);
    mxb_assert(dcb->role() == DCB::Role::BACKEND);

    mxb::atomic::add(&refcount, 1);
    add_backend_conn(conn);
}

void Session::unlink_backend_connection(mxs::BackendConnection* conn)
{
    remove_backend_conn(conn);
    session_put_ref(this);
}

void session_close(MXS_SESSION* ses)
{
    Session* session = static_cast<Session*>(ses);
    session->close();
}

/**
 * Deallocate the specified session
 *
 * @param session       The session to deallocate
 */
static void session_free(MXS_SESSION* session)
{
    MXS_INFO("Stopped %s client session [%" PRIu64 "]", session->service->name(), session->id());
    Service* service = static_cast<Service*>(session->service);

    delete static_cast<Session*>(session);
}

/**
 * Convert a session state to a string representation
 *
 * @param state         The session state
 * @return A string representation of the session state
 */
const char* session_state_to_string(MXS_SESSION::State state)
{
    switch (state)
    {
    case MXS_SESSION::State::CREATED:
        return "Session created";

    case MXS_SESSION::State::STARTED:
        return "Session started";

    case MXS_SESSION::State::STOPPING:
        return "Stopping session";

    case MXS_SESSION::State::FAILED:
        return "Session creation failed";

    case MXS_SESSION::State::FREE:
        return "Freed session";

    default:
        return "Invalid State";
    }
}

/**
 * Return the client connection address or name
 *
 * @param session       The session whose client address to return
 */
const char* session_get_remote(const MXS_SESSION* session)
{
    return session ? session->client_remote().c_str() : nullptr;
}

void Session::deliver_response()
{
    mxb_assert(response.buffer);

    // The reply will always be complete
    mxs::ReplyRoute route;
    mxs::Reply reply;
    response.up->clientReply(response.buffer, route, reply);

    response.up = NULL;
    response.buffer = NULL;

    // If some filter short-circuits the routing, then there will
    // be no response from a server and we need to ensure that
    // subsequent book-keeping targets the right statement.
    book_last_as_complete();

    mxb_assert(!response.up);
    mxb_assert(!response.buffer);
}

bool mxs_route_query(MXS_SESSION* ses, GWBUF* buffer)
{
    Session* session = static_cast<Session*>(ses);
    mxb_assert(session);

    bool rv = session->routeQuery(buffer);

    return rv;
}

bool mxs_route_reply(mxs::Routable* up, GWBUF* buffer, DCB* dcb)
{
    mxs::ReplyRoute route;
    mxs::Reply reply;
    return up->clientReply(buffer, route, reply);
}

/**
 * Return the username of the user connected to the client side of the
 * session.
 *
 * @param session               The session pointer.
 * @return      The user name or NULL if it can not be determined.
 */
const char* session_get_user(const MXS_SESSION* session)
{
    return session ? session->user().c_str() : NULL;
}

static bool ses_find_id(DCB* dcb, void* data)
{
    void** params = (void**)data;
    MXS_SESSION** ses = (MXS_SESSION**)params[0];
    uint64_t* id = (uint64_t*)params[1];
    bool rval = true;

    if (dcb->session()->id() == *id)
    {
        *ses = session_get_ref(dcb->session());
        rval = false;
    }

    return rval;
}

Session* session_get_by_id(uint64_t id)
{
    MXS_SESSION* session = NULL;
    void* params[] = {&session, &id};

    dcb_foreach(ses_find_id, params);

    return static_cast<Session*>(session);
}

MXS_SESSION* session_get_ref(MXS_SESSION* session)
{
    mxb::atomic::add(&session->refcount, 1);
    return session;
}

void session_put_ref(MXS_SESSION* session)
{
    if (session)
    {
        /** Remove one reference. If there are no references left, free session */
        if (mxb::atomic::add(&session->refcount, -1) == 1)
        {
            session_free(session);
        }
    }
}

uint64_t session_get_next_id()
{
    return mxb::atomic::add(&this_unit.next_session_id, 1, mxb::atomic::RELAXED);
}

json_t* Session::as_json_resource(const char* host, bool rdns) const
{
    const char CN_SESSIONS[] = "sessions";

    json_t* data = json_object();

    /** ID must be a string */
    stringstream ss;
    ss << id();

    /** ID and type */
    json_object_set_new(data, CN_ID, json_string(ss.str().c_str()));
    json_object_set_new(data, CN_TYPE, json_string(CN_SESSIONS));

    /** Relationships */
    json_t* rel = json_object();

    /** Service relationship (one-to-one) */
    std::string self = std::string(MXS_JSON_API_SESSIONS) + std::to_string(id()) + "/relationships/";
    json_t* services = mxs_json_relationship(host, self + "services", MXS_JSON_API_SERVICES);
    mxs_json_add_relation(services, service->name(), CN_SERVICES);
    json_object_set_new(rel, CN_SERVICES, services);

    if (!m_filters.empty())
    {
        json_t* filters = mxs_json_relationship(host, self + "filters", MXS_JSON_API_FILTERS);

        for (const auto& f : m_filters)
        {
            mxs_json_add_relation(filters, f.filter->name(), CN_FILTERS);
        }
        json_object_set_new(rel, CN_FILTERS, filters);
    }

    json_object_set_new(data, CN_RELATIONSHIPS, rel);

    /** Session attributes */
    json_t* attr = json_object();
    json_object_set_new(attr, "state", json_string(session_state_to_string(state())));

    if (!user().empty())
    {
        json_object_set_new(attr, CN_USER, json_string(user().c_str()));
    }

    string result_address;
    auto client_dcb = client_connection()->dcb();
    auto& remote = client_dcb->remote();
    if (rdns)
    {
        maxbase::reverse_name_lookup(remote, &result_address);
    }
    else
    {
        result_address = remote;
    }

    json_object_set_new(attr, "remote", json_string(result_address.c_str()));

    struct tm result;
    char buf[60];

    asctime_r(localtime_r(&stats.connect, &result), buf);
    mxb::trim(buf);

    json_object_set_new(attr, "connected", json_string(buf));

    if (client_dcb->state() == DCB::State::POLLING)
    {
        double idle = (mxs_clock() - client_dcb->last_read());
        idle = idle > 0 ? idle / 10.f : 0;
        json_object_set_new(attr, "idle", json_real(idle));
    }

    json_t* connection_arr = json_array();
    for (auto conn : backend_connections())
    {
        json_array_append_new(connection_arr, conn->diagnostics());
    }

    json_object_set_new(attr, "connections", connection_arr);
    json_object_set_new(attr, "client", client_connection()->diagnostics());

    json_t* queries = queries_as_json();
    json_object_set_new(attr, "queries", queries);

    json_t* log = log_as_json();
    json_object_set_new(attr, "log", log);

    json_t* params = json_object();
#ifdef SS_DEBUG
    json_object_set_new(params, "log_debug", json_boolean(log_is_enabled(LOG_DEBUG)));
#endif
    json_object_set_new(params, "log_info", json_boolean(log_is_enabled(LOG_INFO)));
    json_object_set_new(params, "log_notice", json_boolean(log_is_enabled(LOG_NOTICE)));
    json_object_set_new(params, "log_warning", json_boolean(log_is_enabled(LOG_WARNING)));
    json_object_set_new(params, "log_error", json_boolean(log_is_enabled(LOG_ERR)));
    json_object_set_new(attr, CN_PARAMETERS, params);

    json_object_set_new(data, CN_ATTRIBUTES, attr);
    json_object_set_new(data, CN_LINKS, mxs_json_self_link(host, CN_SESSIONS, ss.str().c_str()));

    return data;
}

json_t* session_to_json(const MXS_SESSION* session, const char* host, bool rdns)
{
    stringstream ss;
    ss << MXS_JSON_API_SESSIONS << session->id();
    const Session* s = static_cast<const Session*>(session);
    return mxs_json_resource(host, ss.str().c_str(), s->as_json_resource(host, rdns));
}

struct SessionListData
{
    SessionListData(const char* host, bool rdns)
        : json(json_array())
        , host(host)
        , rdns(rdns)
    {
    }

    json_t*     json {nullptr};
    const char* host {nullptr};
    bool        rdns {false};
};

bool seslist_cb(DCB* dcb, void* data)
{
    if (dcb->role() == DCB::Role::CLIENT)
    {
        SessionListData* d = (SessionListData*)data;
        Session* session = static_cast<Session*>(dcb->session());
        json_array_append_new(d->json, session->as_json_resource(d->host, d->rdns));
    }

    return true;
}

json_t* session_list_to_json(const char* host, bool rdns)
{
    SessionListData data(host, rdns);
    dcb_foreach(seslist_cb, &data);
    return mxs_json_resource(host, MXS_JSON_API_SESSIONS, data.json);
}

MXS_SESSION* session_get_current()
{
    DCB* dcb = dcb_get_current();

    return dcb ? dcb->session() : NULL;
}

uint64_t session_get_current_id()
{
    MXS_SESSION* session = session_get_current();

    return session ? session->id() : 0;
}

bool session_add_variable(MXS_SESSION* session,
                          const char* name,
                          session_variable_handler_t handler,
                          void* context)
{
    Session* pSession = static_cast<Session*>(session);
    return pSession->add_variable(name, handler, context);
}

char* session_set_variable_value(MXS_SESSION* session,
                                 const char* name_begin,
                                 const char* name_end,
                                 const char* value_begin,
                                 const char* value_end)
{
    Session* pSession = static_cast<Session*>(session);
    return pSession->set_variable_value(name_begin, name_end, value_begin, value_end);
}

bool session_remove_variable(MXS_SESSION* session,
                             const char* name,
                             void** context)
{
    Session* pSession = static_cast<Session*>(session);
    return pSession->remove_variable(name, context);
}

void session_set_response(MXS_SESSION* session, SERVICE* service, mxs::Routable* up, GWBUF* buffer)
{
    // Valid arguments.
    mxb_assert(session && up && buffer);

    // Valid state. Only one filter may terminate the execution and exactly once.
    mxb_assert(!session->response.up && !session->response.buffer);

    session->response.up = up;
    session->response.buffer = buffer;
    session->response.service = service;
}

void session_set_retain_last_statements(uint32_t n)
{
    this_unit.retain_last_statements = n;
}

uint32_t session_get_retain_last_statements()
{
    return this_unit.retain_last_statements;
}

void session_set_dump_statements(session_dump_statements_t value)
{
    this_unit.dump_statements = value;
}

session_dump_statements_t session_get_dump_statements()
{
    return this_unit.dump_statements;
}

const char* session_get_dump_statements_str()
{
    switch (this_unit.dump_statements)
    {
    case SESSION_DUMP_STATEMENTS_NEVER:
        return "never";

    case SESSION_DUMP_STATEMENTS_ON_CLOSE:
        return "on_close";

    case SESSION_DUMP_STATEMENTS_ON_ERROR:
        return "on_error";

    default:
        mxb_assert(!true);
        return "unknown";
    }
}

void session_retain_statement(MXS_SESSION* pSession, GWBUF* pBuffer)
{
    static_cast<Session*>(pSession)->retain_statement(pBuffer);
}

void session_book_server_response(MXS_SESSION* pSession, SERVER* pServer, bool final_response)
{
    static_cast<Session*>(pSession)->book_server_response(pServer, final_response);
}

void session_reset_server_bookkeeping(MXS_SESSION* pSession)
{
    static_cast<Session*>(pSession)->reset_server_bookkeeping();
}

void session_dump_statements(MXS_SESSION* session)
{
    Session* pSession = static_cast<Session*>(session);
    pSession->dump_statements();
}

void session_set_session_trace(uint32_t value)
{
    this_unit.session_trace = value;
}

uint32_t session_get_session_trace()
{
    return this_unit.session_trace;
}

void session_append_log(MXS_SESSION* pSession, std::string log)
{
    {
        static_cast<Session*>(pSession)->append_session_log(log);
    }
}

void session_dump_log(MXS_SESSION* pSession)
{
    static_cast<Session*>(pSession)->dump_session_log();
}

class DelayedRoutingTask
{
    DelayedRoutingTask(const DelayedRoutingTask&) = delete;
    DelayedRoutingTask& operator=(const DelayedRoutingTask&) = delete;

public:
    DelayedRoutingTask(MXS_SESSION* session, mxs::Routable* down, GWBUF* buffer)
        : m_session(session_get_ref(session))
        , m_down(down)
        , m_buffer(buffer)
    {
    }

    ~DelayedRoutingTask()
    {
        session_put_ref(m_session);
        gwbuf_free(m_buffer);
    }

    enum Action
    {
        DISPOSE,
        RETAIN
    };

    Action execute()
    {
        Action action = DISPOSE;

        if (m_session->state() == MXS_SESSION::State::STARTED)
        {
            if (mxs::RoutingWorker::get_current() == m_session->worker())
            {
                GWBUF* buffer = m_buffer;
                m_buffer = NULL;

                if (m_down->routeQuery(buffer) == 0)
                {
                    // Routing failed, send a hangup to the client.
                    m_session->client_connection()->dcb()->trigger_hangup_event();
                }
            }
            else
            {
                // Ok, so the session was moved during the delayed call. We need
                // to send the task to that worker.

                DelayedRoutingTask* task = this;

                m_session->worker()->execute([task]() {
                                                 if (task->execute() == DISPOSE)
                                                 {
                                                     delete task;
                                                 }
                                             }, mxb::Worker::EXECUTE_QUEUED);

                action = RETAIN;
            }
        }

        return action;
    }

private:
    MXS_SESSION*   m_session;
    mxs::Routable* m_down;
    GWBUF*         m_buffer;
};

static bool delayed_routing_cb(Worker::Call::action_t action, DelayedRoutingTask* task)
{
    DelayedRoutingTask::Action next_step = DelayedRoutingTask::DISPOSE;

    if (action == Worker::Call::EXECUTE)
    {
        next_step = task->execute();
    }

    if (next_step == DelayedRoutingTask::DISPOSE)
    {
        delete task;
    }

    return false;
}

bool session_delay_routing(MXS_SESSION* session, mxs::Routable* down, GWBUF* buffer, int seconds)
{
    bool success = false;

    try
    {
        Worker* worker = Worker::get_current();
        mxb_assert(worker == session->client_connection()->dcb()->owner);
        std::unique_ptr<DelayedRoutingTask> task(new DelayedRoutingTask(session, down, buffer));

        // Delay the routing for at least a millisecond
        int32_t delay = 1 + seconds * 1000;
        worker->delayed_call(delay, delayed_routing_cb, task.release());

        success = true;
    }
    catch (std::bad_alloc&)
    {
        MXS_OOM();
    }

    return success;
}

const char* session_get_close_reason(const MXS_SESSION* session)
{
    switch (session->close_reason)
    {
    case SESSION_CLOSE_NONE:
        return "";

    case SESSION_CLOSE_TIMEOUT:
        return "Timed out by MaxScale";

    case SESSION_CLOSE_HANDLEERROR_FAILED:
        return "Router could not recover from connection errors";

    case SESSION_CLOSE_ROUTING_FAILED:
        return "Router could not route query";

    case SESSION_CLOSE_KILLED:
        return "Killed by another connection";

    case SESSION_CLOSE_TOO_MANY_CONNECTIONS:
        return "Too many connections";

    default:
        mxb_assert(!true);
        return "Internal error";
    }
}

Session::Session(std::shared_ptr<ListenerSessionData> listener_data,
                 const std::string& host)
    : MXS_SESSION(host, &listener_data->m_service)
    , m_down(static_cast<Service&>(listener_data->m_service).get_connection(this, this))
    , m_routable(this)
    , m_head(&m_routable)
    , m_tail(&m_routable)
    , m_listener_data(std::move(listener_data))
{
    if (service->config()->retain_last_statements != -1)        // Explicitly set for the service
    {
        m_retain_last_statements = service->config()->retain_last_statements;
    }
    else
    {
        m_retain_last_statements = this_unit.retain_last_statements;
    }
}

Session::~Session()
{
    mxb_assert(refcount == 0);
    mxb_assert(!m_down->is_open());

    if (client_dcb)
    {
        delete client_dcb;
        client_dcb = NULL;
    }

    if (this_unit.dump_statements == SESSION_DUMP_STATEMENTS_ON_CLOSE)
    {
        session_dump_statements(this);
    }

    m_state = MXS_SESSION::State::FREE;
}

void Session::set_client_dcb(ClientDCB* dcb)
{
    mxb_assert(client_dcb == nullptr);
    client_dcb = dcb;
}

namespace
{

bool get_cmd_and_stmt(GWBUF* pBuffer, const char** ppCmd, char** ppStmt, int* pLen)
{
    *ppCmd = nullptr;
    *ppStmt = nullptr;
    *pLen = 0;

    bool deallocate = false;
    int len = gwbuf_length(pBuffer);

    if (len > MYSQL_HEADER_LEN)
    {
        uint8_t header[MYSQL_HEADER_LEN + 1];
        uint8_t* pHeader = NULL;

        if (gwbuf_link_length(pBuffer) > MYSQL_HEADER_LEN)
        {
            pHeader = GWBUF_DATA(pBuffer);
        }
        else
        {
            gwbuf_copy_data(pBuffer, 0, MYSQL_HEADER_LEN + 1, header);
            pHeader = header;
        }

        int cmd = MYSQL_GET_COMMAND(pHeader);

        *ppCmd = STRPACKETTYPE(cmd);

        if (cmd == MXS_COM_QUERY)
        {
            if (gwbuf_is_contiguous(pBuffer))
            {
                modutil_extract_SQL(pBuffer, ppStmt, pLen);
            }
            else
            {
                *ppStmt = modutil_get_SQL(pBuffer);
                *pLen = strlen(*ppStmt);
                deallocate = true;
            }
        }
    }

    return deallocate;
}
}

void Session::dump_statements() const
{
    if (m_retain_last_statements)
    {
        int n = m_last_queries.size();

        uint64_t current_id = session_get_current_id();

        if ((current_id != 0) && (current_id != id()))
        {
            MXS_WARNING("Current session is %lu, yet statements are dumped for %lu. "
                        "The session id in the subsequent dumped statements is the wrong one.",
                        current_id, id());
        }

        for (auto i = m_last_queries.rbegin(); i != m_last_queries.rend(); ++i)
        {
            const QueryInfo& info = *i;
            GWBUF* pBuffer = info.query().get();
            timespec ts = info.time_completed();
            struct tm* tm = localtime(&ts.tv_sec);
            char timestamp[20];
            strftime(timestamp, 20, "%Y-%m-%d %H:%M:%S", tm);

            const char* pCmd;
            char* pStmt;
            int len;
            bool deallocate = get_cmd_and_stmt(pBuffer, &pCmd, &pStmt, &len);

            if (pStmt)
            {
                if (current_id != 0)
                {
                    MXS_NOTICE("Stmt %d(%s): %.*s", n, timestamp, len, pStmt);
                }
                else
                {
                    // We are in a context where we do not have a current session, so we need to
                    // log the session id ourselves.

                    MXS_NOTICE("(%" PRIu64 ") Stmt %d(%s): %.*s", id(), n, timestamp, len, pStmt);
                }

                if (deallocate)
                {
                    MXS_FREE(pStmt);
                }
            }

            --n;
        }
    }
}

json_t* Session::queries_as_json() const
{
    json_t* pQueries = json_array();

    for (auto i = m_last_queries.rbegin(); i != m_last_queries.rend(); ++i)
    {
        const QueryInfo& info = *i;

        json_array_append_new(pQueries, info.as_json());
    }

    return pQueries;
}

json_t* Session::log_as_json() const
{
    json_t* pLog = json_array();

    for (const auto& i : m_log)
    {
        json_array_append_new(pLog, json_string(i.c_str()));
    }

    return pLog;
}

bool Session::add_variable(const char* name, session_variable_handler_t handler, void* context)
{
    bool added = false;

    static const char PREFIX[] = "@MAXSCALE.";

    if (strncasecmp(name, PREFIX, sizeof(PREFIX) - 1) == 0)
    {
        string key(name);

        std::transform(key.begin(), key.end(), key.begin(), tolower);

        if (m_variables.find(key) == m_variables.end())
        {
            SESSION_VARIABLE variable;
            variable.handler = handler;
            variable.context = context;

            m_variables.insert(std::make_pair(key, variable));
            added = true;
        }
        else
        {
            MXS_ERROR("Session variable '%s' has been added already.", name);
        }
    }
    else
    {
        MXS_ERROR("Session variable '%s' is not of the correct format.", name);
    }

    return added;
}

char* Session::set_variable_value(const char* name_begin,
                                  const char* name_end,
                                  const char* value_begin,
                                  const char* value_end)
{
    char* rv = NULL;

    string key(name_begin, name_end - name_begin);

    transform(key.begin(), key.end(), key.begin(), tolower);

    auto it = m_variables.find(key);

    if (it != m_variables.end())
    {
        rv = it->second.handler(it->second.context, key.c_str(), value_begin, value_end);
    }
    else
    {
        const char FORMAT[] = "Attempt to set unknown MaxScale user variable %.*s";

        int name_length = name_end - name_begin;
        int len = snprintf(NULL, 0, FORMAT, name_length, name_begin);

        rv = static_cast<char*>(MXS_MALLOC(len + 1));

        if (rv)
        {
            sprintf(rv, FORMAT, name_length, name_begin);
        }

        MXS_WARNING(FORMAT, name_length, name_begin);
    }

    return rv;
}

bool Session::remove_variable(const char* name, void** context)
{
    bool removed = false;
    string key(name);

    transform(key.begin(), key.end(), key.begin(), toupper);
    auto it = m_variables.find(key);

    if (it != m_variables.end())
    {
        if (context)
        {
            *context = it->second.context;
        }

        m_variables.erase(it);
        removed = true;
    }

    return removed;
}

void Session::retain_statement(GWBUF* pBuffer)
{
    if (m_retain_last_statements)
    {
        mxb_assert(m_last_queries.size() <= m_retain_last_statements);

        std::shared_ptr<GWBUF> sBuffer(gwbuf_clone(pBuffer), std::default_delete<GWBUF>());

        m_last_queries.push_front(QueryInfo(sBuffer));

        if (m_last_queries.size() > m_retain_last_statements)
        {
            m_last_queries.pop_back();
        }

        if (m_last_queries.size() == 1)
        {
            mxb_assert(m_current_query == -1);
            m_current_query = 0;
        }
        else
        {
            // If requests are streamed, without the response being waited for,
            // then this may cause the index to grow past the length of the array.
            // That's ok and is dealt with in book_server_response() and friends.
            ++m_current_query;
            mxb_assert(m_current_query >= 0);
        }
    }
}

void Session::book_server_response(SERVER* pServer, bool final_response)
{
    if (m_retain_last_statements && !m_last_queries.empty())
    {
        mxb_assert(m_current_query >= 0);
        // If enough queries have been sent by the client, without it waiting
        // for the responses, then at this point it may be so that the query
        // object has been popped from the size limited queue. That's apparent
        // by the index pointing past the end of the queue. In that case
        // we simply ignore the result.
        if (m_current_query < static_cast<int>(m_last_queries.size()))
        {
            auto i = m_last_queries.begin() + m_current_query;
            QueryInfo& info = *i;

            mxb_assert(!info.complete());

            info.book_server_response(pServer, final_response);
        }

        if (final_response)
        {
            // In case what is described in the comment above has occurred,
            // this will eventually take the index back into the queue.
            --m_current_query;
            mxb_assert(m_current_query >= -1);
        }
    }
}

void Session::book_last_as_complete()
{
    if (m_retain_last_statements && !m_last_queries.empty())
    {
        mxb_assert(m_current_query >= 0);
        // See comment in book_server_response().
        if (m_current_query < static_cast<int>(m_last_queries.size()))
        {
            auto i = m_last_queries.begin() + m_current_query;
            QueryInfo& info = *i;

            info.book_as_complete();
        }
    }
}

void Session::reset_server_bookkeeping()
{
    if (m_retain_last_statements && !m_last_queries.empty())
    {
        mxb_assert(m_current_query >= 0);
        // See comment in book_server_response().
        if (m_current_query < static_cast<int>(m_last_queries.size()))
        {
            auto i = m_last_queries.begin() + m_current_query;
            QueryInfo& info = *i;
            info.reset_server_bookkeeping();
        }
    }
}

Session::QueryInfo::QueryInfo(const std::shared_ptr<GWBUF>& sQuery)
    : m_sQuery(sQuery)
{
    clock_gettime(CLOCK_REALTIME_COARSE, &m_received);
    m_completed.tv_sec = 0;
    m_completed.tv_nsec = 0;
}

namespace
{

static const char ISO_TEMPLATE[] = "2018-11-05T16:47:49.123";
static const int ISO_TIME_LEN = sizeof(ISO_TEMPLATE) - 1;

void timespec_to_iso(char* zIso, const timespec& ts)
{
    tm tm;
    localtime_r(&ts.tv_sec, &tm);

    size_t i = strftime(zIso, ISO_TIME_LEN + 1, "%G-%m-%dT%H:%M:%S", &tm);
    mxb_assert(i == 19);
    long int ms = ts.tv_nsec / 1000000;
    i = sprintf(zIso + i, ".%03ld", ts.tv_nsec / 1000000);
    mxb_assert(i == 4);
}
}

json_t* Session::QueryInfo::as_json() const
{
    json_t* pQuery = json_object();

    const char* pCmd;
    char* pStmt;
    int len;
    bool deallocate = get_cmd_and_stmt(m_sQuery.get(), &pCmd, &pStmt, &len);

    if (pCmd)
    {
        json_object_set_new(pQuery, "command", json_string(pCmd));
    }

    if (pStmt)
    {
        json_object_set_new(pQuery, "statement", json_stringn(pStmt, len));

        if (deallocate)
        {
            MXS_FREE(pStmt);
        }
    }

    char iso_time[ISO_TIME_LEN + 1];

    timespec_to_iso(iso_time, m_received);
    json_object_set_new(pQuery, "received", json_stringn(iso_time, ISO_TIME_LEN));

    if (m_complete)
    {
        timespec_to_iso(iso_time, m_completed);
        json_object_set_new(pQuery, "completed", json_stringn(iso_time, ISO_TIME_LEN));
    }

    json_t* pResponses = json_array();

    for (auto& info : m_server_infos)
    {
        json_t* pResponse = json_object();

        // Calculate and report in milliseconds.
        long int received = m_received.tv_sec * 1000 + m_received.tv_nsec / 1000000;
        long int processed = info.processed.tv_sec * 1000 + info.processed.tv_nsec / 1000000;
        mxb_assert(processed >= received);

        long int duration = processed - received;

        json_object_set_new(pResponse, "server", json_string(info.pServer->name()));
        json_object_set_new(pResponse, "duration", json_integer(duration));

        json_array_append_new(pResponses, pResponse);
    }

    json_object_set_new(pQuery, "responses", pResponses);

    return pQuery;
}

void Session::QueryInfo::book_server_response(SERVER* pServer, bool final_response)
{
    // If the information has been completed, no more information may be provided.
    mxb_assert(!m_complete);
    // A particular server may be reported only exactly once.
    mxb_assert(find_if(m_server_infos.begin(), m_server_infos.end(), [pServer](const ServerInfo& info) {
                           return info.pServer == pServer;
                       }) == m_server_infos.end());

    timespec now;
    clock_gettime(CLOCK_REALTIME_COARSE, &now);

    m_server_infos.push_back(ServerInfo {pServer, now});

    m_complete = final_response;

    if (m_complete)
    {
        m_completed = now;
    }
}

void Session::QueryInfo::book_as_complete()
{
    timespec now;
    clock_gettime(CLOCK_REALTIME_COARSE, &m_completed);
    m_complete = true;
}

void Session::QueryInfo::reset_server_bookkeeping()
{
    m_server_infos.clear();
    m_completed.tv_sec = 0;
    m_completed.tv_nsec = 0;
    m_complete = false;
}

bool Session::start()
{
    bool rval = false;

    if (m_down->connect())
    {
        rval = true;
        m_state = MXS_SESSION::State::STARTED;

        MXS_INFO("Started %s client session [%" PRIu64 "] for '%s' from %s",
                 service->name(), id(),
                 !m_user.empty() ? m_user.c_str() : "<no user>",
                 m_client_conn->dcb()->remote().c_str());
    }

    return rval;
}

void Session::close()
{
    m_state = State::STOPPING;
    m_down->close();
}

void Session::append_session_log(std::string log)
{
    m_log.push_front(log);

    if (m_log.size() >= this_unit.session_trace)
    {
        m_log.pop_back();
    }
}

void Session::dump_session_log()
{
    if (!(m_log.empty()))
    {
        std::string log;

        for (const auto& s : m_log)
        {
            log += s;
        }

        MXS_NOTICE("Session log for session (%" PRIu64 "): \n%s ", id(), log.c_str());
    }
}

int32_t Session::routeQuery(GWBUF* buffer)
{
    if (m_rebuild_chain && is_idle())
    {
        m_filters = std::move(m_pending_filters);
        m_rebuild_chain = false;
        setup_routing_chain();
    }

    auto rv = m_head->routeQuery(buffer);

    if (response.buffer)
    {
        // Something interrupted the routing and queued a response
        deliver_response();
    }

    return rv;
}

int32_t Session::clientReply(GWBUF* buffer, mxs::ReplyRoute& down, const mxs::Reply& reply)
{
    return m_tail->clientReply(buffer, down, reply);
}

bool Session::handleError(mxs::ErrorType type, GWBUF* error, Endpoint* down, const mxs::Reply& reply)
{
    kill(gwbuf_clone(error));
    return false;
}

mxs::ClientConnection* Session::client_connection()
{
    return m_client_conn;
}

const mxs::ClientConnection* Session::client_connection() const
{
    return m_client_conn;
}

void Session::set_client_connection(mxs::ClientConnection* client_conn)
{
    m_client_conn = client_conn;
}

void Session::add_backend_conn(mxs::BackendConnection* conn)
{
    mxb_assert(std::find(m_backends_conns.begin(), m_backends_conns.end(), conn) == m_backends_conns.end());
    m_backends_conns.push_back(conn);
}

void Session::remove_backend_conn(mxs::BackendConnection* conn)
{
    auto iter = std::find(m_backends_conns.begin(), m_backends_conns.end(), conn);
    mxb_assert(iter != m_backends_conns.end());
    m_backends_conns.erase(iter);
}

mxs::BackendConnection*
Session::create_backend_connection(Server* server, BackendDCB::Manager* manager, mxs::Component* upstream)
{
    std::unique_ptr<BackendConnection> conn;
    auto proto_module = m_listener_data->m_proto_module.get();
    if (proto_module->capabilities() & mxs::ProtocolModule::CAP_BACKEND)
    {
        conn = proto_module->create_backend_protocol(this, server, upstream);
        if (!conn)
        {
            MXS_ERROR("Failed to create protocol session for backend DCB.");
        }
    }
    else
    {
        MXB_ERROR("Protocol '%s' does not support backend connections.", proto_module->name().c_str());
    }

    BackendDCB* dcb = nullptr;
    if (conn)
    {
        dcb = BackendDCB::connect(server, this, manager);
        if (dcb)
        {
            conn->set_dcb(dcb);
            auto pConn = conn.get();
            link_backend_connection(pConn);
            dcb->set_connection(std::move(conn));
            dcb->reset(this);

            if (dcb->enable_events())
            {
                // The DCB is now connected and added to epoll set. Authentication is done after the EPOLLOUT
                // event that is triggered once the connection is established.
            }
            else
            {
                unlink_backend_connection(pConn);
                DCB::destroy(dcb);
                dcb = nullptr;
            }
        }
    }
    return dcb->protocol();
}

void Session::tick(int64_t idle)
{
    const auto& svc_config = *service->config();
    if (auto timeout = svc_config.conn_idle_timeout)
    {
        if (idle > timeout)
        {
            MXS_WARNING("Timing out %s, idle for %ld seconds", user_and_host().c_str(), idle);
            close_reason = SESSION_CLOSE_TIMEOUT;
            kill();
        }
    }

    if (auto net_timeout = svc_config.net_write_timeout)
    {
        if (idle > net_timeout && client_dcb->writeq_len() > 0)
        {
            MXS_WARNING("Network write timed out for %s.", user_and_host().c_str());
            close_reason = SESSION_CLOSE_TIMEOUT;
            kill();
        }
    }

    if (auto interval = svc_config.connection_keepalive)
    {
        for (const auto& a : backend_connections())
        {
            if (a->seconds_idle() > interval && a->is_idle())
            {
                a->ping();
            }
        }
    }

    if (m_ttl && MXS_CLOCK_TO_SEC(mxs_clock() - m_ttl_start) > m_ttl)
    {
        MXS_WARNING("Killing session %lu, session TTL exceeded.", id());
        kill();
    }
}

void Session::set_ttl(int64_t ttl)
{
    m_ttl = ttl;
    m_ttl_start = mxs_clock();
}

bool Session::is_idle() const
{
    // TODO: This is a placeholder. The is_movable isn't query-aware and a separate is_idle method is needed.
    return m_client_conn->is_idle()
           && std::all_of(m_backends_conns.begin(), m_backends_conns.end(),
                          std::mem_fn(&mxs::BackendConnection::is_idle));
}

void Session::update_log_level(json_t* param, const char* key, int level)
{
    if (json_t* log_level = json_object_get(param, key))
    {
        if (json_is_boolean(log_level))
        {
            if (json_boolean_value(log_level))
            {
                m_log_level |= (1 << level);
            }
            else
            {
                m_log_level &= ~(1 << level);
            }
        }
    }
}

bool Session::update(json_t* json)
{
    bool rval = true;

    if (json_t* param = mxs_json_pointer(json, MXS_JSON_PTR_PARAMETERS))
    {
#ifdef SS_DEBUG
        update_log_level(param, "log_debug", LOG_DEBUG);
#endif
        update_log_level(param, "log_info", LOG_INFO);
        update_log_level(param, "log_notice", LOG_NOTICE);
        update_log_level(param, "log_warning", LOG_WARNING);
        update_log_level(param, "log_error", LOG_ERR);
    }

    if (json_t* rel = mxs_json_pointer(json, "/data/relationships/filters/data"))
    {
        decltype(m_filters) new_filters;
        size_t idx;
        json_t* val;

        json_array_foreach(rel, idx, val)
        {
            json_t* name = json_object_get(val, CN_ID);

            if (json_is_string(name))
            {
                if (auto f = filter_find(json_string_value(name)))
                {
                    new_filters.emplace_back(f);
                    auto& sf = new_filters.back();
                    sf.session.reset(sf.instance->newSession(this, service));

                    if (!sf.session)
                    {
                        MXS_ERROR("Failed to create filter session for '%s'", sf.filter->name());
                        return false;
                    }
                }
            }
        }

        if (is_idle())
        {
            m_filters = std::move(new_filters);
            setup_routing_chain();
        }
        else
        {
            m_pending_filters = std::move(new_filters);
            m_rebuild_chain = true;
        }
    }

    return rval;
}

void Session::setup_routing_chain()
{
    mxs::Routable* chain_head = &m_routable;

    for (auto it = m_filters.rbegin(); it != m_filters.rend(); it++)
    {
        it->session->setDownstream(chain_head);
        it->down = chain_head;
        chain_head = it->session.get();
    }

    m_head = chain_head;

    mxs::Routable* chain_tail = &m_routable;

    for (auto it = m_filters.begin(); it != m_filters.end(); it++)
    {
        it->session->setUpstream(chain_tail);
        it->up = chain_tail;
        chain_tail = it->session.get();
    }

    m_tail = chain_tail;
}

// static
void Session::foreach(std::function<void(Session*)> func)
{
    mxs::RoutingWorker::execute_concurrently(
        [func]() {
            for (auto kv : mxs::RoutingWorker::get_current()->session_registry())
            {
                func(static_cast<Session*>(kv.second));
            }
        });
}

// static
void Session::kill_all(SERVICE* service)
{
    Session::foreach(
        [service](Session* session) {
            if (session->service == service)
            {
                session->kill();
            }
        });
}

// static
void Session::kill_all(Listener* listener)
{
    Session::foreach(
        [listener](Session* session) {
            if (session->listener_data()->m_listener_name == listener->name())
            {
                session->kill();
            }
        });
}

ListenerSessionData* Session::listener_data()
{
    return m_listener_data.get();
}

void Session::adjust_io_activity(time_t now) const
{
    int secs = now - m_last_io_activity;
    if (secs == 0)
    {
        // Session is being frequently used, several updates during one second.
        // The load values need not be adjusted.
    }
    else
    {
        // TODO: Change this into two indexes.

        // There has been secs seconds during which the session has not been used.
        if (secs < N_LOAD)
        {
            // If secs is less than 30, then we need to move the values from the
            // beginning as many steps to the right.
            std::copy_backward(m_io_activity.begin(), m_io_activity.end() - secs, m_io_activity.end());
        }

        // And fill from the beginning with zeros. If secs >= 30, the whole
        // array will be zeroed.
        std::fill(m_io_activity.begin(), m_io_activity.begin() + std::min(secs, N_LOAD), 0);
    }
}

void Session::book_io_activity()
{
    time_t now = time(nullptr);
    adjust_io_activity(now);

    ++m_io_activity[0];

    m_last_io_activity = now;
}

int Session::io_activity() const
{
    adjust_io_activity(time(nullptr));

    return std::accumulate(m_io_activity.begin(), m_io_activity.end(), 0);
}

namespace
{

bool enable_events(const std::vector<DCB*>& dcbs)
{
    bool enabled = true;

    for (DCB* pDcb : dcbs)
    {
        if (!pDcb->enable_events())
        {
            MXS_ERROR("Could not re-enable epoll events, session will be terminated.");
            enabled = false;
            break;
        }
    }

    return enabled;
}
}

bool Session::move_to(RoutingWorker* pTo)
{
    mxs::RoutingWorker* pFrom = RoutingWorker::get_current();
    mxb_assert(m_worker == pFrom);
    // TODO: Change to MXS_INFO when everything is ready.
    MXS_NOTICE("Moving session from %d to %d.", pFrom->id(), pTo->id());

    std::vector<DCB*> to_be_enabled;

    DCB* pDcb = m_client_conn->dcb();

    if (pDcb->is_polling())
    {
        pDcb->disable_events();
        to_be_enabled.push_back(pDcb);
    }
    pDcb->set_owner(nullptr);
    pDcb->set_manager(nullptr);

    for (mxs::BackendConnection* backend_conn : m_backends_conns)
    {
        pDcb = backend_conn->dcb();
        if (pDcb->is_polling())
        {
            pDcb->disable_events();
            to_be_enabled.push_back(pDcb);
        }
        pDcb->set_owner(nullptr);
        pDcb->set_manager(nullptr);
    }

    pFrom->session_registry().remove(id());

    m_worker = pTo;     // Set before the move-operation, see DelayedRoutingTask.

    bool posted = pTo->execute([this, pFrom, pTo, to_be_enabled]() {
                                   pTo->session_registry().add(this);

                                   m_client_conn->dcb()->set_owner(pTo);
                                   m_client_conn->dcb()->set_manager(pTo);

                                   for (mxs::BackendConnection* pBackend_conn : m_backends_conns)
                                   {
                                       pBackend_conn->dcb()->set_owner(pTo);
                                       pBackend_conn->dcb()->set_manager(pTo);
                                   }

                                   if (!enable_events(to_be_enabled))
                                   {
                                       kill();
                                   }

                                   MXS_NOTICE("Moved session from %d to %d.", pFrom->id(), pTo->id());
                               }, mxb::Worker::EXECUTE_QUEUED);

    if (!posted)
    {
        MXS_ERROR("Could not move session from worker %d to worker %d.",
                  pFrom->id(), pTo->id());

        m_worker = pFrom;

        m_client_conn->dcb()->set_owner(pFrom);
        m_client_conn->dcb()->set_manager(pFrom);

        for (mxs::BackendConnection* pBackend_conn : m_backends_conns)
        {
            pBackend_conn->dcb()->set_owner(pFrom);
            pBackend_conn->dcb()->set_manager(pFrom);
        }

        pFrom->session_registry().add(this);

        if (!enable_events(to_be_enabled))
        {
            MXS_ERROR("Could not re-enable epoll events, session will be terminated.");
            kill();
        }
    }

    return posted;
}

bool Session::is_movable() const
{
    if (m_client_conn && !m_client_conn->is_movable())
    {
        return false;
    }

    for (auto backend_conn : m_backends_conns)
    {
        if (!backend_conn->is_movable())
        {
            return false;
        }
    }
    return true;
}

void Session::notify_userdata_change()
{
    for (auto* subscriber : m_event_subscribers)
    {
        subscriber->userdata_changed();
    }
}

void Session::add_userdata_subscriber(MXS_SESSION::EventSubscriber* obj)
{
    mxb_assert(m_event_subscribers.count(obj) == 0);
    m_event_subscribers.insert(obj);
}

void Session::remove_userdata_subscriber(MXS_SESSION::EventSubscriber* obj)
{
    mxb_assert(m_event_subscribers.count(obj) == 1);
    m_event_subscribers.erase(obj);
}

MXS_SESSION::EventSubscriber::EventSubscriber(MXS_SESSION* session)
    : m_session(session)
{
    m_session->add_userdata_subscriber(this);
}

MXS_SESSION::EventSubscriber::~EventSubscriber()
{
    m_session->remove_userdata_subscriber(this);
}
