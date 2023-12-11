/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * @file session.c  - A representation of the session within the gateway.
 */
#include "internal/session.hh"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <string>
#include <sstream>
#include <utility>

#include <maxbase/alloc.hh>
#include <maxbase/atomic.hh>
#include <maxbase/format.hh>
#include <maxbase/host.hh>
#include <maxscale/clock.hh>
#include <maxscale/cn_strings.hh>
#include <maxscale/dcb.hh>
#include <maxscale/http.hh>
#include <maxscale/json_api.hh>
#include <maxscale/listener.hh>
#include <maxscale/router.hh>
#include <maxscale/routingworker.hh>
#include <maxscale/service.hh>

#include "internal/dcb.hh"
#include "internal/filter.hh"
#include "internal/server.hh"
#include "internal/service.hh"

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

struct ThisThread
{
    MXS_SESSION* session = nullptr;
};

thread_local ThisThread this_thread;

uint64_t session_get_next_id()
{
    return mxb::atomic::add(&this_unit.next_session_id, 1, mxb::atomic::RELAXED);
}
}

// static
const int Session::N_LOAD;

MXS_SESSION::MXS_SESSION(const std::string& host, SERVICE* service)
    : mxb::Worker::Callable(mxs::RoutingWorker::get_current())
    , m_state(MXS_SESSION::State::CREATED)
    , m_id(session_get_next_id())
    , m_host(host)
    , m_capabilities(service->capabilities() | RCAP_TYPE_REQUEST_TRACKING)
    , client_dcb(nullptr)
    , service(service)
    , refcount(1)
    , response{}
    , close_reason(SESSION_CLOSE_NONE)
{
    worker()->register_session(this);
}

MXS_SESSION::~MXS_SESSION()
{
    cancel_dcalls();
    mxb_assert(mxs::RoutingWorker::get_current() == worker());
    worker()->deregister_session(m_id);
}

maxscale::RoutingWorker* MXS_SESSION::worker() const
{
    return static_cast<maxscale::RoutingWorker*>(Callable::worker());
}

void MXS_SESSION::kill(const std::string& errmsg)
{
    if (!m_killed && (m_state == State::CREATED || m_state == State::STARTED))
    {
        mxb_assert(client_connection()->dcb()->is_open());
        m_killed = true;
        close_reason = SESSION_CLOSE_HANDLEERROR_FAILED;

        // Call the protocol kill function before changing the session state
        client_connection()->kill(errmsg);

        if (m_state == State::STARTED)
        {
            // Disable dcalls immediately after the session is killed. This simplifies the logic by removing
            // the need to check if the session is still alive when the dcall is executed. The dcalls cannot
            // be cancelled as it is possible that a dcall calls MXS_SESSION::kill() which ends up deleting
            // the dcall while it's being called.
            if (!dcalls_suspended())
            {
                suspend_dcalls();
            }

            // This signals the rest of the system that the session has started the shutdown procedure.
            // Currently it mainly affects debug assertions inside the protocol code.
            m_state = State::STOPPING;
        }

        ClientDCB::close(client_dcb);
    }
}

mxs::ProtocolData* MXS_SESSION::protocol_data() const
{
    return m_protocol_data.get();
}

void MXS_SESSION::set_protocol_data(std::unique_ptr<mxs::ProtocolData> new_data)
{
    m_protocol_data = std::move(new_data);
}

void Session::link_backend_connection(mxs::BackendConnection* conn)
{
    auto dcb = conn->dcb();
    mxb_assert(dcb->owner() == m_client_conn->dcb()->owner());
    mxb_assert(dcb->role() == DCB::Role::BACKEND);

    mxb::atomic::add(&refcount, 1);
    add_backend_conn(conn);
}

void Session::unlink_backend_connection(mxs::BackendConnection* conn)
{
    remove_backend_conn(conn);
    session_put_ref(this);
}

/**
 * Deallocate the specified session
 *
 * @param session       The session to deallocate
 */
static void session_free(MXS_SESSION* session)
{
    MXB_INFO("Stopped %s client session [%" PRIu64 "]", session->service->name(), session->id());
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

mxb::Json Session::get_memory_statistics() const
{
    mxb::Json memory;

    size_t connection_buffers;
    size_t last_queries;
    size_t variables;

    size_t total = get_memory_statistics(&connection_buffers, &last_queries, &variables);

    mxb::Json cb;
    cb.set_int("total", connection_buffers);

    mxb_assert(m_client_conn && m_client_conn->dcb());
    cb.set_object("client", m_client_conn->dcb()->get_memory_statistics());

    mxb::Json backends;
    for (const auto* conn : m_backend_conns)
    {
        auto* dcb = conn->dcb();
        mxb_assert(dcb);
        backends.set_object(dcb->server()->name(), dcb->get_memory_statistics());
    }

    cb.set_object("backends", std::move(backends));

    memory.set_object("connection_buffers", std::move(cb));

    memory.set_int("last_queries", last_queries);
    memory.set_int("variables", variables);

    const auto* p = protocol_data();

    if (p)
    {
        total += p->amend_memory_statistics(memory.get_json());
    }

    memory.set_int("total", total);

    return memory;
}

size_t Session::static_size() const
{
    return sizeof(*this);
}

size_t Session::varying_size() const
{
    size_t total = get_memory_statistics(nullptr, nullptr, nullptr);

    const auto* p = protocol_data();

    if (p)
    {
        total += p->runtime_size();
    }

    return total;
}

size_t Session::get_memory_statistics(size_t* connection_buffers_size,
                                      size_t* last_queries_size,
                                      size_t* variables_size) const
{
    size_t connection_buffers = 0;
    mxb_assert(m_client_conn);
    connection_buffers += m_client_conn->sizeof_buffers();
    for (const auto* conn : m_backend_conns)
    {
        connection_buffers += conn->sizeof_buffers();
    }

    size_t last_queries = 0;
    for (const auto& qi : m_last_queries)
    {
        last_queries += qi.runtime_size();
    }

    size_t variables = 0;
    for (const auto& kv : m_variables)
    {
        variables += sizeof(kv);
        variables += kv.first.capacity();
    }

    if (connection_buffers_size)
    {
        *connection_buffers_size = connection_buffers;
    }

    if (last_queries_size)
    {
        *last_queries_size = last_queries;
    }

    if (variables_size)
    {
        *variables_size = variables;
    }

    return connection_buffers + last_queries + variables;
}

void MXS_SESSION::deliver_response()
{
    mxb_assert(!response.buffer.empty());
    mxb_assert(response.up);
    auto buffer = std::make_shared<GWBUF>(std::exchange(response.buffer, GWBUF()));
    auto up = std::exchange(response.up, nullptr);
    auto ref = up->endpoint().shared_from_this();

    worker()->lcall([this, up, buffer, ref](){
        if (ref->is_open())
        {
            // The reply will always be complete
            mxs::ReplyRoute route;
            mxs::Reply reply;
            up->clientReply(buffer->shallow_clone(), route, reply);
        }
    });
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

uint64_t session_max_id()
{
    return mxb::atomic::load(&this_unit.next_session_id, mxb::atomic::RELAXED) - 1;
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
    if (rdns && !mxs::Config::get().skip_name_resolve.get())
    {
        maxbase::reverse_name_lookup(remote, &result_address);
    }
    else
    {
        result_address = remote;
    }

    json_object_set_new(attr, "remote", json_string(result_address.c_str()));
    json_object_set_new(attr, "port", json_integer(client_dcb->port()));

    json_object_set_new(attr, "connected", json_string(http_to_date(m_connected).c_str()));
    json_object_set_new(attr, "seconds_alive", json_real(mxb::to_secs(mxb::Clock::now() - m_started)));

    if (client_dcb->state() == DCB::State::POLLING)
    {
        auto idle = client_connection()->is_idle() ? mxb::to_secs(client_dcb->idle_time()) : 0.0;
        json_object_set_new(attr, "idle", json_real(idle));
    }

    mxb::Json memory = get_memory_statistics();
    json_object_set_new(attr, "memory", memory.release());
    json_object_set_new(attr, "io_activity", json_integer(io_activity()));

    json_t* connection_arr = json_array();
    for (auto conn : backend_connections())
    {
        json_array_append_new(connection_arr, conn->diagnostics());
    }

    json_object_set_new(attr, "connections", connection_arr);
    json_object_set_new(attr, "client", client_connection()->diagnostics());
    json_object_set_new(attr, "thread", json_integer(worker()->index()));

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

bool Session::can_pool_backends() const
{
    return m_can_pool_backends;
}

void Session::set_can_pool_backends(bool value)
{
    if (value)
    {
        if (m_pooling_time >= 0ms)
        {
            // If pooling check was already scheduled, do nothing. This likely only happens when killing
            // an idle session.
            if (m_idle_pool_call_id == mxb::Worker::NO_CALL)
            {
                if (m_pooling_time > 0ms)
                {
                    m_idle_pool_call_id = dcall(m_pooling_time, &Session::pool_backends_cb, this);
                }
                else
                {
                    auto func = [this]() {
                        pool_backends_cb(Worker::Callable::Action::EXECUTE);
                    };
                    worker()->lcall(std::move(func));
                }
            }
        }
    }
    else if (m_idle_pool_call_id != mxb::Worker::NO_CALL)
    {
        cancel_dcall(m_idle_pool_call_id);
    }

    m_can_pool_backends = value;
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

    return dcb ? dcb->session() : this_thread.session;
}

uint64_t session_get_current_id()
{
    MXS_SESSION* session = session_get_current();

    return session ? session->id() : 0;
}

MXS_SESSION::Scope::Scope(MXS_SESSION* session)
    : m_prev(std::exchange(this_thread.session, session))
{
}

MXS_SESSION::Scope::~Scope()
{
    this_thread.session = m_prev;
}

void session_set_response(MXS_SESSION* session, mxs::Routable* up, GWBUF&& buffer)
{
    // Valid arguments.
    mxb_assert(session && up);

    // Valid state. Only one filter may terminate the execution and exactly once.
    mxb_assert(!session->response.up && session->response.buffer.empty());

    session->response.up = up;
    session->response.buffer = std::move(buffer);
}

bool session_has_response(MXS_SESSION* session)
{
    return !session->response.buffer.empty();
}

GWBUF session_release_response(MXS_SESSION* session)
{
    mxb_assert(session_has_response(session));

    GWBUF rv(std::move(session->response.buffer));

    session->response.up = nullptr;
    session->response.buffer.clear();

    return rv;
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

void session_set_session_trace(uint32_t value)
{
    this_unit.session_trace = value;
}

uint32_t session_get_session_trace()
{
    return this_unit.session_trace;
}

void MXS_SESSION::delay_routing(mxs::Routable* down, GWBUF&& buffer, std::chrono::milliseconds delay,
                                std::function<bool(GWBUF &&)>&& fn)
{
    mxb_assert_message(down->endpoint().parent(),
                       "delay_routing() must only be called from a non-root Component");

    auto sbuf = std::make_shared<GWBUF>(std::move(buffer));
    auto ref = down->endpoint().shared_from_this();
    auto cb = [this, fn, sbuf = std::move(sbuf), ref = std::move(ref)](mxb::Worker::Callable::Action action){
        if (action == mxb::Worker::Callable::EXECUTE && ref->is_open())
        {
            MXS_SESSION::Scope scope(this);
            mxb_assert(state() == MXS_SESSION::State::STARTED);

            if (!fn(std::move(*sbuf)))
            {
                // Routing the query failed. Let parent component deal with it in handleError. This must
                // currently be treated as a permanent error, otherwise it could result in an infinite
                // retrying loop.
                ref->parent()->handleError(mxs::ErrorType::PERMANENT, "Failed to route query",
                                           const_cast<mxs::Endpoint*>(ref.get()), mxs::Reply {});
            }

            if (ref->is_open() && !response.buffer.empty())
            {
                // Something interrupted the routing and queued a response
                deliver_response();
            }
        }

        return false;
    };

    if (delay.count() == 0)
    {
        worker()->lcall([wrapped_cb = std::move(cb)](){
            wrapped_cb(mxb::Worker::Callable::EXECUTE);
        });
    }
    else
    {
        dcall(delay, std::move(cb));
    }
}

void MXS_SESSION::delay_routing(mxs::Routable* down, GWBUF&& buffer, std::chrono::milliseconds delay)
{
    delay_routing(down, std::move(buffer), delay, [down](GWBUF&& buf){
        return down->routeQuery(std::move(buf));
    });
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

Session::Session(std::shared_ptr<const ListenerData> listener_data,
                 std::shared_ptr<const mxs::ConnectionMetadata> metadata,
                 SERVICE* service, const std::string& host)
    : MXS_SESSION(host, service)
    , m_down(static_cast<Service&>(*service).get_connection(this, this))
    , m_connected(time(0))
    , m_started(mxb::Clock::now())
    , m_routable(this)
    , m_head(&m_routable)
    , m_tail(&m_routable)
    , m_listener_data(std::move(listener_data))
    , m_metadata(std::move(metadata))
    , m_suspend(service->is_suspended())
 {
    const auto& svc_config = *service->config();
    if (svc_config.retain_last_statements != -1)        // Explicitly set for the service
    {
        m_retain_last_statements = svc_config.retain_last_statements;
    }
    else
    {
        m_retain_last_statements = this_unit.retain_last_statements;
    }

    // Connection sharing related settings are pinned at session creation. Service config changes only affect
    // new sessions.
    m_pooling_time = svc_config.idle_session_pool_time;
    m_multiplex_timeout = svc_config.multiplex_timeout;
}

Session::~Session()
{
    mxb_assert(refcount == 0);
    mxb_assert(!m_down->is_open());

    if (m_idle_pool_call_id != mxb::Worker::NO_CALL)
    {
        cancel_dcall(m_idle_pool_call_id);
    }
    if (this_unit.dump_statements == SESSION_DUMP_STATEMENTS_ON_CLOSE)
    {
        dump_statements();
    }

    if (!m_log.empty())
    {
        if (auto re = mxs::Config::get().session_trace_match.get())
        {
            for (const auto& [_, line] : m_log)
            {
                if (re.match(line))
                {
                    dump_session_log();
                    break;
                }
            }
        }
    }

    service->track_session_duration(mxb::Clock::now() - m_started);

    // dump_statements() above needs the client connection, which is owned by
    // the DCB, so delete the DCB as the last thing.
    if (client_dcb)
    {
        delete client_dcb;
        client_dcb = NULL;
    }

    m_state = MXS_SESSION::State::FREE;
}

namespace
{

void get_cmd_and_stmt(const mxs::Parser::Helper& helper,
                      const GWBUF& buffer, const char** ppCmd, const char** ppStmt, int* pLen)
{
    *ppCmd = nullptr;
    *ppStmt = nullptr;
    *pLen = 0;

    std::string_view sql = helper.get_sql(buffer);

    if (!sql.empty())
    {
        auto cmd = helper.get_command(buffer);
        *ppCmd = helper.client_command_to_string(cmd);
        *ppStmt = sql.data();
        *pLen = sql.length();
    }
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
            MXB_WARNING("Current session is %lu, yet statements are dumped for %lu. "
                        "The session id in the subsequent dumped statements is the wrong one.",
                        current_id, id());
        }

        auto& helper = client_connection()->parser()->helper();
        for (auto i = m_last_queries.rbegin(); i != m_last_queries.rend(); ++i)
        {
            const QueryInfo& info = *i;
            const auto& buffer = info.query();
            timespec ts = info.time_completed();
            struct tm* tm = localtime(&ts.tv_sec);
            char timestamp[20];
            strftime(timestamp, 20, "%Y-%m-%d %H:%M:%S", tm);

            const char* pCmd;
            const char* pStmt;
            int len;
            get_cmd_and_stmt(helper, buffer, &pCmd, &pStmt, &len);

            if (pStmt)
            {
                if (current_id != 0)
                {
                    MXB_NOTICE("Stmt %d(%s): %.*s", n, timestamp, len, pStmt);
                }
                else
                {
                    // We are in a context where we do not have a current session, so we need to
                    // log the session id ourselves.

                    MXB_NOTICE("(%" PRIu64 ") Stmt %d(%s): %.*s", id(), n, timestamp, len, pStmt);
                }
            }

            --n;
        }
    }
}

json_t* Session::queries_as_json() const
{
    json_t* pQueries = json_array();

    const auto& helper = client_connection()->parser()->helper();
    for (auto i = m_last_queries.rbegin(); i != m_last_queries.rend(); ++i)
    {
        const QueryInfo& info = *i;

        json_array_append_new(pQueries, info.as_json(helper));
    }

    return pQueries;
}

json_t* Session::log_as_json() const
{
    json_t* pLog = json_array();

    for (const auto& [tv, msg] : m_log)
    {
        auto str = mxb::cat(mxb::format_timestamp(tv, true), msg);
        json_array_append_new(pLog, json_string(str.c_str()));
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
            MXB_ERROR("Session variable '%s' has been added already.", name);
        }
    }
    else
    {
        MXB_ERROR("Session variable '%s' is not of the correct format.", name);
    }

    return added;
}

string Session::set_variable_value(const char* name_begin,
                                   const char* name_end,
                                   const char* value_begin,
                                   const char* value_end)
{
    string rv;

    string key(name_begin, name_end - name_begin);

    transform(key.begin(), key.end(), key.begin(), tolower);

    auto it = m_variables.find(key);

    if (it != m_variables.end())
    {
        char* temp = it->second.handler(it->second.context, key.c_str(), value_begin, value_end);
        if (temp)
        {
            rv = temp;
            MXB_FREE(temp);
        }
    }
    else
    {
        const char FORMAT[] = "Attempt to set unknown MaxScale user variable %.*s";
        int name_length = name_end - name_begin;
        rv = mxb::string_printf(FORMAT, name_length, name_begin);
        MXB_WARNING("%s", rv.c_str());
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

void Session::retain_statement(const GWBUF& buffer)
{
    if (m_retain_last_statements)
    {
        mxb_assert(m_last_queries.size() <= m_retain_last_statements);
        if (m_last_queries.size() >= m_retain_last_statements)
        {
            m_last_queries.pop_back();
        }
        m_last_queries.emplace_front(buffer.deep_clone());

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

void Session::book_server_response(mxs::Target* pTarget, bool final_response)
{
    if (m_retain_last_statements && !m_last_queries.empty())
    {
        bool found = false;
        for (SERVER* s : static_cast<Service*>(this->service)->reachable_servers())
        {
            if (static_cast<mxs::Target*>(s) == pTarget)
            {
                found = true;
                break;
            }
        }

        if (!found)
        {
            // Not a server
            return;
        }

        SERVER* pServer = static_cast<SERVER*>(pTarget);

        mxb_assert(m_current_query >= 0);

        if (m_current_query < 0)
        {
            MXB_ALERT("Internal logic error, disabling retain_last_statements.");
            m_retain_last_statements = 0;
            return;
        }

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

Session::QueryInfo::QueryInfo(GWBUF query)
    : m_query(std::move(query))
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

json_t* Session::QueryInfo::as_json(const mxs::Parser::Helper& helper) const
{
    json_t* pQuery = json_object();

    const char* pCmd;
    const char* pStmt;
    int len;
    get_cmd_and_stmt(helper, m_query, &pCmd, &pStmt, &len);

    if (pCmd)
    {
        json_object_set_new(pQuery, "command", json_string(pCmd));
    }

    if (pStmt)
    {
        json_object_set_new(pQuery, "statement", json_stringn(pStmt, len));
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

        MXB_INFO("Started %s client session [%" PRIu64 "] for '%s' from %s on '%s'",
                 service->name(), id(),
                 !m_user.empty() ? m_user.c_str() : "<no user>",
                 m_client_conn->dcb()->remote().c_str(),
                 worker()->name());
    }

    return rval;
}

void Session::close()
{
    cancel_dcalls();
    m_state = State::STOPPING;
    m_down->close();
}

bool Session::suspend()
{
    bool rv = false;

    if (!m_suspend)
    {
        m_suspend = true;

        if (is_enabled())
        {
            if (is_idle() && !is_in_trx())
            {
                disable_events();
                rv = true;
            }
        }
    }
    else
    {
        rv = !is_enabled();
    }

    return rv;
}

bool Session::resume()
{
    bool rv = false;

    if (m_suspend)
    {
        if (!is_enabled())
        {
            enable_events();
            rv = true;
        }

        m_suspend = false;
    }

    return rv;
}

void Session::enable_events()
{
    mxb_assert(!m_enabled);

    mxb_assert(m_client_conn);
    bool success = m_client_conn->dcb()->enable_events();

    if (success)
    {
        for (auto* conn : m_backend_conns)
        {
            mxb_assert(conn->dcb());
            success = conn->dcb()->enable_events();

            if (!success)
            {
                break;
            }
        }
    }

    if (success)
    {
        m_enabled = true;
    }
    else
    {
        kill("Could not enable events on some connection of the session.");
    }
}

void Session::disable_events()
{
    mxb_assert(m_enabled);

    mxb_assert(m_client_conn);
    bool success = m_client_conn->dcb()->disable_events();

    if (success)
    {
        for (auto* conn : m_backend_conns)
        {
            mxb_assert(conn->dcb());
            success = conn->dcb()->disable_events();

            if (!success)
            {
                break;
            }
        }
    }

    if (success)
    {
        m_enabled = false;
    }
    else
    {
        kill("Could not disable events on some connection of the session.");
    }
}

bool Session::restart()
{
    bool ok = true;

    if (idle_pooling_enabled())
    {
        MXB_ERROR("Cannot restart session if 'idle_session_pool_time' is in use.");
        ok = false;
    }
    else if (have_dcalls())
    {
        // TODO: This is not that good. With a large amount of sessions the likelihood of a restart increases
        // and with some workloads it might never work. A better approach would be to assume that the
        // dcalls will eventually disappear and retry the restart until it succeeds. This of course is not
        // guaranteed to ever happen which is why a timeout is needed for any automated operations.
        MXB_ERROR("Cannot restart session due to ongoing internal operations. Try again later.");
        ok = false;
    }
    else
    {
        m_restart = true;
        ok = true;
    }

    return ok;
}

void Session::do_restart()
{
    mxb_assert(!idle_pooling_enabled());

    if (have_dcalls())
    {
        MXB_WARNING("Cannot do planned restart of session due to ongoing internal operations.");
    }
    else
    {
        auto down = static_cast<Service&>(*this->service).get_connection(this, this);

        if (down->connect())
        {
            m_down->close();
            m_down = std::move(down);
        }
    }

    // Regardless of whether the operation was successful, don't try to restart again.
    m_restart = false;
}

void Session::append_session_log(struct timeval tv, std::string_view msg)
{
    if (!m_dumping_log)
    {
        if (m_log.capacity() != this_unit.session_trace)
        {
            Log tmp(this_unit.session_trace, std::move(m_log));
            m_log = std::move(tmp);
        }

        m_log.push(std::make_pair(tv, msg));
    }
}

void Session::dump_session_log()
{
    // Logging the messages with MXB_NOTICE will cause the in-memory log handler to be called which would end
    // up modifying the log while we iterate over it. Even if it didn't invalidate the iterators, it would
    // still end up replacing the contents with the new messages that are about to be logged.
    m_dumping_log = true;

    for (auto [ts, msg] : m_log)
    {
        // Both the original message and this new message will contain the session ID in them. To make it easy
        // to filter the log output to just the original message, a prefix of ### Trace ### is added to all
        // messages. This also helps identify which ones are trace log messages and which ones are other info
        // messages from things like session-level or service-level info logging.
        MXB_NOTICE("### Trace ### %s%s", mxb::format_timestamp(ts, true).c_str(), msg.c_str());
    }

    m_dumping_log = false;
}

bool Session::routeQuery(GWBUF&& buffer)
{
    mxb_assert(buffer);

    if (m_restart || m_rebuild_chain)
    {
        if (is_idle() && m_client_conn->safe_to_restart())
        {
            if (m_restart)
            {
                do_restart();
            }

            if (m_rebuild_chain)
            {
                m_filters = std::move(m_pending_filters);
                m_pending_filters.clear();
                m_rebuild_chain = false;
                setup_routing_chain();
            }
        }
    }

    mxb_assert(!m_routing);
    MXB_AT_DEBUG(m_routing = true);

    auto rv = m_head->routeQuery(std::move(buffer));

    MXB_AT_DEBUG(m_routing = false);

    if (!response.buffer.empty())
    {
        // Something interrupted the routing and queued a response
        deliver_response();
    }

    return rv;
}

bool Session::clientReply(GWBUF&& buffer, const mxs::ReplyRoute& down, const mxs::Reply& reply)
{
    mxb_assert(!m_routing);
    mxb_assert(buffer);

    bool rv = m_tail->clientReply(std::move(buffer), down, reply);

    if (m_suspend && is_idle() && (!is_in_trx() || is_trx_ending()))
    {
        mxb_assert(is_enabled());
        disable_events();
    }

    return rv;
}

bool Session::handleError(mxs::ErrorType type, const std::string& error,
                          Endpoint* down, const mxs::Reply& reply)
{
    // Log the error since it is what caused the session to close
    MXB_ERROR("%s", error.c_str());
    kill();
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
    mxb_assert(!m_client_conn && !client_dcb);

    m_client_conn = client_conn;
    client_dcb = m_client_conn->dcb();

    mxb_assert(client_dcb);
}

void Session::add_backend_conn(mxs::BackendConnection* conn)
{
    mxb_assert(std::find(m_backend_conns.begin(), m_backend_conns.end(), conn) == m_backend_conns.end());
    m_backend_conns.push_back(conn);
}

void Session::remove_backend_conn(mxs::BackendConnection* conn)
{
    auto iter = std::find(m_backend_conns.begin(), m_backend_conns.end(), conn);
    mxb_assert(iter != m_backend_conns.end());
    m_backend_conns.erase(iter);
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
            MXB_ERROR("Failed to create protocol session for backend DCB.");
        }
    }
    else
    {
        MXB_ERROR("Protocol '%s' does not support backend connections.", proto_module->name().c_str());
    }

    BackendDCB* dcb = nullptr;
    mxs::BackendConnection* ret = nullptr;
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
                ret = dcb->protocol();
            }
            else
            {
                unlink_backend_connection(pConn);
                DCB::destroy(dcb);
                dcb = nullptr;
            }
        }
    }
    return ret;
}

void Session::tick(int64_t idle)
{
    Scope scope(this);
    m_client_conn->tick(std::chrono::seconds(idle));

    const auto& svc_config = *service->config();
    if (auto timeout = svc_config.conn_idle_timeout.count())
    {
        if (idle > timeout && is_idle())
        {
            MXB_WARNING("Timing out %s, idle for %ld seconds", user_and_host().c_str(), idle);
            close_reason = SESSION_CLOSE_TIMEOUT;
            kill();
        }
    }

    if (auto net_timeout = svc_config.net_write_timeout.count())
    {
        if (idle > net_timeout && client_dcb->writeq_len() > 0)
        {
            MXB_WARNING("Network write timed out for %s.", user_and_host().c_str());
            close_reason = SESSION_CLOSE_TIMEOUT;
            kill();
        }
    }

    if (auto interval = svc_config.connection_keepalive.count())
    {
        if (svc_config.force_connection_keepalive
            || client_connection()->dcb()->seconds_idle() < interval
            || !client_connection()->is_idle())
        {
            for (const auto& a : backend_connections())
            {
                if (a->dcb()->seconds_idle() > interval && a->is_idle())
                {
                    a->ping();
                }
            }
        }
    }

    if (m_ttl && MXS_CLOCK_TO_SEC(mxs_clock() - m_ttl_start) > m_ttl)
    {
        MXB_WARNING("Killing session %lu, session TTL exceeded.", id());
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
    return m_client_conn->is_idle()
           && std::all_of(m_backend_conns.begin(), m_backend_conns.end(),
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

    if (json_t* param = mxb::json_ptr(json, MXS_JSON_PTR_PARAMETERS))
    {
#ifdef SS_DEBUG
        update_log_level(param, "log_debug", LOG_DEBUG);
#endif
        update_log_level(param, "log_info", LOG_INFO);
        update_log_level(param, "log_notice", LOG_NOTICE);
        update_log_level(param, "log_warning", LOG_WARNING);
        update_log_level(param, "log_error", LOG_ERR);
    }

    if (json_t* rel = mxb::json_ptr(json, "/data/relationships/filters/data"))
    {
        decltype(m_filters) new_filters;
        size_t idx;
        json_t* val;

        json_array_foreach(rel, idx, val)
        {
            json_t* name = json_object_get(val, CN_ID);

            if (json_is_string(name))
            {
                const char* filter_name = json_string_value(name);

                if (auto f = filter_find(filter_name))
                {
                    new_filters.emplace_back(f);
                    auto& sf = new_filters.back();
                    sf.session.reset(sf.instance->newSession(this, service));

                    if (!sf.session)
                    {
                        MXB_ERROR("Failed to create filter session for '%s'", sf.filter->name());
                        return false;
                    }
                }
                else
                {
                    MXB_ERROR("Not a valid filter: %s", filter_name);
                    return false;
                }
            }
            else
            {
                MXB_ERROR("Not a JSON string but a %s", mxb::json_type_to_string(name));
                return false;
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

    // TODO: The capabilities of these filters aren't taken into account
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

const ListenerData* Session::listener_data() const
{
    return m_listener_data.get();
}

const mxs::ProtocolModule* Session::protocol() const
{
    return listener_data()->m_proto_module.get();
}

void Session::adjust_io_activity(time_t now) const
{
    int secs = now - m_last_io_activity;
    if (secs <= 0)
    {
        // Session is being frequently used, several updates during one second. The load values need not be
        // adjusted. If the value is negative, the clock has gone backwards and we just have to ignore this.
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

bool enable_events_for(const std::vector<DCB*>& dcbs)
{
    bool enabled = true;

    for (DCB* pDcb : dcbs)
    {
        if (!pDcb->enable_events())
        {
            MXB_ERROR("Could not re-enable epoll events, session will be terminated.");
            enabled = false;
            break;
        }
    }

    return enabled;
}
}

bool Session::move_to(RoutingWorker* pTo)
{
    mxs::RoutingWorker* pFrom = worker();
    mxb_assert(RoutingWorker::get_current() == pFrom);
    // TODO: Change to MXB_INFO when everything is ready.
    MXB_NOTICE("Moving session from %d to %d.", pFrom->id(), pTo->id());

    suspend_dcalls();
    set_worker(nullptr);

    std::vector<DCB*> to_be_enabled;

    DCB* pDcb = m_client_conn->dcb();

    if (pDcb->is_polling())
    {
        pDcb->disable_events();
        to_be_enabled.push_back(pDcb);
    }
    pDcb->set_owner(nullptr);
    pDcb->set_manager(nullptr);

    if (m_idle_pool_call_id != mxb::Worker::NO_CALL)
    {
        cancel_dcall(m_idle_pool_call_id);
    }

    for (mxs::BackendConnection* backend_conn : m_backend_conns)
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

    auto receive_session = [this, pFrom, pTo, to_be_enabled]() {
        pTo->session_registry().add(this);

        m_client_conn->dcb()->set_owner(pTo);
        m_client_conn->dcb()->set_manager(pTo);

        for (mxs::BackendConnection* pBackend_conn : m_backend_conns)
        {
            pBackend_conn->dcb()->set_owner(pTo);
            pBackend_conn->dcb()->set_manager(pTo);
        }

        if (enable_events_for(to_be_enabled))
        {
            if (m_can_pool_backends)
            {
                // Schedule another check.
                set_can_pool_backends(true);
            }
        }
        else
        {
            kill();
        }

        set_worker(pTo);
        resume_dcalls();

        MXB_NOTICE("Moved session from %d to %d.", pFrom->id(), pTo->id());
    };
    bool posted = pTo->execute(receive_session, mxb::Worker::EXECUTE_QUEUED);

    if (!posted)
    {
        MXB_ERROR("Could not move session from worker %d to worker %d.",
                  pFrom->id(), pTo->id());

        m_client_conn->dcb()->set_owner(pFrom);
        m_client_conn->dcb()->set_manager(pFrom);

        for (mxs::BackendConnection* pBackend_conn : m_backend_conns)
        {
            pBackend_conn->dcb()->set_owner(pFrom);
            pBackend_conn->dcb()->set_manager(pFrom);
        }

        pFrom->session_registry().add(this);

        if (!enable_events_for(to_be_enabled))
        {
            MXB_ERROR("Could not re-enable epoll events, session will be terminated.");
            kill();
        }

        set_worker(pFrom);
        resume_dcalls();
    }

    return posted;
}

bool Session::is_movable() const
{
    if (m_client_conn && !m_client_conn->is_movable())
    {
        return false;
    }

    for (auto backend_conn : m_backend_conns)
    {
        if (!backend_conn->is_movable())
        {
            return false;
        }
    }

    // Do not move a session which may be waiting for a connection.
    return !idle_pooling_enabled();
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

bool Session::pool_backends_cb(mxb::Worker::Callable::Action action)
{
    // This should only be called by the delayed call mechanism.
    bool call_again = true;
    if (action == Worker::Callable::EXECUTE)
    {
        MXS_SESSION::Scope scope(this);
        // Session has been idle for long enough, check that the connections have not had
        // any activity.
        auto* client = client_dcb;
        if (client->state() == DCB::State::POLLING)
        {
            const auto& backends = backend_connections();
            size_t n_pooled = 0;

            // Pooling modifies the backends-vector. Use a temporary array.
            size_t n_backends = backends.size();
            std::vector<ServerEndpoint*> poolable_eps;

            for (auto& backend : backends)
            {
                if (backend->established() && backend->is_idle())
                {
                    auto pEp = static_cast<ServerEndpoint*>(backend->upstream());
                    if (worker()->conn_to_server_needed(pEp->server()))
                    {
                        poolable_eps.push_back(pEp);
                    }
                }
            }

            for (auto* pEp : poolable_eps)
            {
                if (pEp->try_to_pool())
                {
                    n_pooled++;
                }
                // TODO: If pooling failed, increase time until next check to reduce rate of
                // pointless pooling attempts.
            }
            if (n_pooled == n_backends)
            {
                // TODO: Think if there is some corner case where a connection is
                // reattached to a session but clientReply is not called.
                call_again = false;
            }
        }

        if (!call_again)
        {
            // Need to remove this manually as cancel-mode is not called.
            m_idle_pool_call_id = mxb::Worker::NO_CALL;
        }
        else if (m_pooling_time < 1s)
        {
            // Returning true means the delayed call will run again after 'm_pooling_time'.
            // This is ok if the time is several seconds, as some connections may not yet have
            // been in a poolable state. This is not so ok if the time is very short. Enforce
            // a minimum delay.

            // TODO: Nicer if delayed call could modify its own timing.
            call_again = false;
            m_idle_pool_call_id = dcall(1000ms, &Session::pool_backends_cb, this);
        }
    }
    else
    {
        m_idle_pool_call_id = mxb::Worker::NO_CALL;
    }
    return call_again;
}

bool Session::idle_pooling_enabled() const
{
    return m_pooling_time >= 0ms;
}

std::chrono::seconds Session::multiplex_timeout() const
{
    return m_multiplex_timeout;
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

bool MXS_SESSION::log_is_enabled(int level) const
{
    return m_log_level & (1 << level) || service->log_is_enabled(level);
}

void MXS_SESSION::set_host(string&& host)
{
    m_host = std::move(host);
}

namespace maxscale
{
void unexpected_situation(const char* msg)
{
    if (MXS_SESSION* ses = session_get_current())
    {
        if (this_unit.session_trace)
        {
            ses->dump_session_log();
        }
        else
        {
            MXB_WARNING("MaxScale has encountered an unexpected situation: %s. Add 'session_trace=200' "
                        "under the [maxscale] section to enable session level tracing to make the "
                        "debugging of this problem easier.", msg);
        }
    }
}
}
