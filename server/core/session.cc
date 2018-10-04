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

/**
 * @file session.c  - A representation of the session within the gateway.
 */
#include <maxscale/session.h>

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <algorithm>
#include <string>
#include <sstream>

#include <maxscale/alloc.h>
#include <maxbase/atomic.hh>
#include <maxscale/clock.h>
#include <maxscale/dcb.h>
#include <maxscale/housekeeper.h>
#include <maxscale/log.h>
#include <maxscale/poll.h>
#include <maxscale/router.h>
#include <maxscale/service.h>
#include <maxscale/utils.h>
#include <maxscale/json_api.h>
#include <maxscale/protocol/mysql.h>
#include <maxscale/routingworker.hh>

#include "internal/dcb.h"
#include "internal/filter.hh"
#include "internal/session.hh"
#include "internal/service.hh"

using std::string;
using std::stringstream;
using maxbase::Worker;
using namespace maxscale;

/** Global session id counter. Must be updated atomically. Value 0 is reserved for
 *  dummy/unused sessions.
 */
static uint64_t next_session_id = 1;

static uint32_t retain_last_statements = 0;
static session_dump_statements_t dump_statements = SESSION_DUMP_STATEMENTS_NEVER;

namespace
{

static struct session dummy_session()
{
    struct session session = {};
    session.state = SESSION_STATE_DUMMY;
    session.refcount = 1;
    return session;
}
}

static struct session session_dummy_struct = dummy_session();

static void         session_initialize(void* session);
static int          session_setup_filters(MXS_SESSION* session);
static void         session_simple_free(MXS_SESSION* session, DCB* dcb);
static void         session_add_to_all_list(MXS_SESSION* session);
static MXS_SESSION* session_find_free();
static void         session_final_free(MXS_SESSION* session);
static MXS_SESSION* session_alloc_body(SERVICE* service,
                                       DCB* client_dcb,
                                       MXS_SESSION* session,
                                       uint64_t id);
static void session_deliver_response(MXS_SESSION* session);

/**
 * The clientReply of the session.
 *
 * @param inst     In reality an MXS_SESSION*.
 * @param session  In reality an MXS_SESSION*.
 * @param data     The data to send to the client.
 */
static int session_reply(MXS_FILTER* inst, MXS_FILTER_SESSION* session, GWBUF* data);

MXS_SESSION* session_alloc(SERVICE* service, DCB* client_dcb)
{
    return session_alloc_with_id(service, client_dcb, session_get_next_id());
}

MXS_SESSION* session_alloc_with_id(SERVICE* service, DCB* client_dcb, uint64_t id)
{
    Session* session = new(std::nothrow) Session;

    if (session == nullptr)
    {
        return NULL;
    }

    return session_alloc_body(service, client_dcb, session, id);
}

static MXS_SESSION* session_alloc_body(SERVICE* service,
                                       DCB* client_dcb,
                                       MXS_SESSION* session,
                                       uint64_t id)
{
    session->state = SESSION_STATE_READY;
    session->ses_id = id;
    session->client_dcb = client_dcb;
    session->router_session = NULL;
    session->stats.connect = time(0);
    session->service = service;
    memset(&session->head, 0, sizeof(session->head));
    memset(&session->tail, 0, sizeof(session->tail));
    session->load_active = false;

    /*<
     * Associate the session to the client DCB and set the reference count on
     * the session to indicate that there is a single reference to the
     * session. There is no need to protect this or use atomic add as the
     * session has not been made available to the other threads at this
     * point.
     *
     * Note: Strictly speaking, we do have to increment it with a relaxed atomic
     *       operation but in practice it doesn't matter.
     */
    session->refcount = 1;
    session->trx_state = SESSION_TRX_INACTIVE;

    MXS_CONFIG* config = config_get_global_options();
    // If MaxScale is running in Oracle mode, then autocommit needs to
    // initially be off.
    bool autocommit = (config->qc_sql_mode == QC_SQL_MODE_ORACLE) ? false : true;
    session_set_autocommit(session, autocommit);

    session->client_protocol_data = 0;
    session->qualifies_for_pooling = false;
    memset(&session->response, 0, sizeof(session->response));
    session->close_reason = SESSION_CLOSE_NONE;

    /*
     * Only create a router session if we are not the listening DCB or an
     * internal DCB. Creating a router session may create a connection to
     * a backend server, depending upon the router module implementation
     * and should be avoided for a listener session.
     *
     * Router session creation may create other DCBs that link to the
     * session.
     */
    if (client_dcb->state != DCB_STATE_LISTENING
        && client_dcb->dcb_role != DCB_ROLE_INTERNAL)
    {
        session->router_session = service->router->newSession(service->router_instance, session);
        if (session->router_session == NULL)
        {
            session->state = SESSION_STATE_TO_BE_FREED;
            MXS_ERROR("Failed to create new router session for service '%s'. "
                      "See previous errors for more details.",
                      service->name);
        }
        /*
         * Pending filter chain being setup set the head of the chain to
         * be the router. As filters are inserted the current head will
         * be pushed to the filter and the head updated.
         *
         * NB This dictates that filters are created starting at the end
         * of the chain nearest the router working back to the client
         * protocol end of the chain.
         */
        // NOTE: Here we cast the router instance into a MXS_FILTER and
        // NOTE: the router session into a MXS_FILTER_SESSION and
        // NOTE: the router routeQuery into a filter routeQuery. That
        // NOTE: is in order to be able to treat the router as the first
        // NOTE: filter.
        session->head = router_as_downstream(session);

        // NOTE: Here we cast the session into a MXS_FILTER and MXS_FILTER_SESSION
        // NOTE: and session_reply into a filter clientReply. That's dubious but ok
        // NOTE: as session_reply will know what to do. In practice, the session
        // NOTE: will be called as if it would be the last filter.
        session->tail.instance = (MXS_FILTER*)session;
        session->tail.session = (MXS_FILTER_SESSION*)session;
        session->tail.clientReply = session_reply;

        if (SESSION_STATE_TO_BE_FREED != session->state
            && !session_setup_filters(session))
        {
            session->state = SESSION_STATE_TO_BE_FREED;
            MXS_ERROR("Setting up filters failed. "
                      "Terminating session %s.",
                      service->name);
        }
    }

    if (SESSION_STATE_TO_BE_FREED != session->state)
    {
        session->state = SESSION_STATE_ROUTER_READY;

        if (session->client_dcb->user == NULL)
        {
            MXS_INFO("Started session [%" PRIu64 "] for %s service ",
                     session->ses_id,
                     service->name);
        }
        else
        {
            MXS_INFO("Started %s client session [%" PRIu64 "] for '%s' from %s",
                     service->name,
                     session->ses_id,
                     session->client_dcb->user,
                     session->client_dcb->remote);
        }
    }
    else
    {
        MXS_INFO("Start %s client session [%" PRIu64 "] for '%s' from %s failed, will be "
                                                     "closed as soon as all related DCBs have been closed.",
                 service->name,
                 session->ses_id,
                 session->client_dcb->user,
                 session->client_dcb->remote);
    }
    mxb::atomic::add(&service->stats.n_sessions, 1, mxb::atomic::RELAXED);
    mxb::atomic::add(&service->stats.n_current, 1, mxb::atomic::RELAXED);

    // Store the session in the client DCB even if the session creation fails.
    // It will be freed later on when the DCB is closed.
    client_dcb->session = session;

    return (session->state == SESSION_STATE_TO_BE_FREED) ? NULL : session;
}

/**
 * Allocate a dummy session so that DCBs can always have sessions.
 *
 * Only one dummy session exists, it is statically declared
 *
 * @param client_dcb    The client side DCB
 * @return              The dummy created session
 */
MXS_SESSION* session_set_dummy(DCB* client_dcb)
{
    MXS_SESSION* session = &session_dummy_struct;
    client_dcb->session = session;
    return session;
}

void session_link_backend_dcb(MXS_SESSION* session, DCB* dcb)
{
    mxb_assert(dcb->dcb_role == DCB_ROLE_BACKEND_HANDLER);

    mxb::atomic::add(&session->refcount, 1);
    dcb->session = session;
    dcb->service = session->service;
    /** Move this DCB under the same thread */
    dcb->poll.owner = session->client_dcb->poll.owner;

    Session* ses = static_cast<Session*>(session);
    ses->link_backend_dcb(dcb);
}

void session_unlink_backend_dcb(MXS_SESSION* session, DCB* dcb)
{
    Session* ses = static_cast<Session*>(session);
    ses->unlink_backend_dcb(dcb);
    session_put_ref(session);
}

/**
 * Deallocate the specified session, minimal actions during session_alloc
 * Since changes to keep new session in existence until all related DCBs
 * have been destroyed, this function is redundant.  Just left until we are
 * sure of the direction taken.
 *
 * @param session       The session to deallocate
 */
static void session_simple_free(MXS_SESSION* session, DCB* dcb)
{
    /* Does this possibly need a lock? */
    if (dcb->data)
    {
        void* clientdata = dcb->data;
        dcb->data = NULL;
        MXS_FREE(clientdata);
    }
    if (session)
    {
        if (SESSION_STATE_DUMMY == session->state)
        {
            return;
        }
        if (session && session->router_session)
        {
            session->service->router->freeSession(session->service->router_instance,
                                                  session->router_session);
        }
        session->state = SESSION_STATE_STOPPING;
    }

    session_final_free(session);
}

void session_close(MXS_SESSION* session)
{
    if (session->router_session)
    {
        session->state = SESSION_STATE_STOPPING;

        MXS_ROUTER_OBJECT* router = session->service->router;
        MXS_ROUTER* router_instance = session->service->router_instance;

        /** Close router session and all its connections */
        router->closeSession(router_instance, session->router_session);
    }
}

class ServiceDestroyTask : public Worker::DisposableTask
{
public:
    ServiceDestroyTask(Service* service)
        : m_service(service)
    {
    }

    void execute(Worker& worker) override
    {
        service_free(m_service);
    }

private:
    Service* m_service;
};

/**
 * Deallocate the specified session
 *
 * @param session       The session to deallocate
 */
static void session_free(MXS_SESSION* session)
{
    MXS_INFO("Stopped %s client session [%" PRIu64 "]", session->service->name, session->ses_id);
    Service* service = static_cast<Service*>(session->service);

    session_final_free(session);
    bool should_destroy = !mxb::atomic::load(&service->active);

    if (mxb::atomic::add(&service->client_count, -1) == 1 && should_destroy)
    {
        // Destroy the service in the main routing worker thread
        mxs::RoutingWorker* main_worker = mxs::RoutingWorker::get(mxs::RoutingWorker::MAIN);
        main_worker->execute(std::unique_ptr<ServiceDestroyTask>(new ServiceDestroyTask(service)),
                             Worker::EXECUTE_AUTO);
    }
}

static void session_final_free(MXS_SESSION* ses)
{
    Session* session = static_cast<Session*>(ses);
    mxb_assert(session->refcount == 0);

    session->state = SESSION_STATE_TO_BE_FREED;

    mxb::atomic::add(&session->service->stats.n_current, -1, mxb::atomic::RELAXED);

    if (session->client_dcb)
    {
        dcb_free_all_memory(session->client_dcb);
        session->client_dcb = NULL;
    }

    if (dump_statements == SESSION_DUMP_STATEMENTS_ON_CLOSE)
    {
        session_dump_statements(session);
    }

    session->state = SESSION_STATE_FREE;

    delete session;
}

/**
 * Check to see if a session is valid, i.e. in the list of all sessions
 *
 * @param session       Session to check
 * @return              1 if the session is valid otherwise 0
 */
int session_isvalid(MXS_SESSION* session)
{
    return session != NULL;
}

/**
 * Print details of an individual session
 *
 * @param session       Session to print
 */
void printSession(MXS_SESSION* session)
{
    struct tm result;
    char timebuf[40];

    printf("Session %p\n", session);
    printf("\tState:        %s\n", session_state(session->state));
    printf("\tService:      %s (%p)\n", session->service->name, session->service);
    printf("\tClient DCB:   %p\n", session->client_dcb);
    printf("\tConnected:    %s\n",
           asctime_r(localtime_r(&session->stats.connect, &result), timebuf));
    printf("\tRouter Session: %p\n", session->router_session);
}

bool printAllSessions_cb(DCB* dcb, void* data)
{
    if (dcb->dcb_role == DCB_ROLE_CLIENT_HANDLER)
    {
        printSession(dcb->session);
    }

    return true;
}

/**
 * Print all sessions
 *
 * Designed to be called within a debugger session in order
 * to display all active sessions within the gateway
 */
void printAllSessions()
{
    dcb_foreach(printAllSessions_cb, NULL);
}

/** Callback for dprintAllSessions */
bool dprintAllSessions_cb(DCB* dcb, void* data)
{
    if (dcb->dcb_role == DCB_ROLE_CLIENT_HANDLER
        && dcb->session->state != SESSION_STATE_DUMMY)
    {
        DCB* out_dcb = (DCB*)data;
        dprintSession(out_dcb, dcb->session);
    }
    return true;
}

/**
 * Print all sessions to a DCB
 *
 * Designed to be called within a debugger session in order
 * to display all active sessions within the gateway
 *
 * @param dcb   The DCB to print to
 */
void dprintAllSessions(DCB* dcb)
{
    dcb_foreach(dprintAllSessions_cb, dcb);
}

/**
 * Print a particular session to a DCB
 *
 * Designed to be called within a debugger session in order
 * to display all active sessions within the gateway
 *
 * @param dcb   The DCB to print to
 * @param print_session   The session to print
 */
void dprintSession(DCB* dcb, MXS_SESSION* print_session)
{
    struct tm result;
    char buf[30];
    int i;

    dcb_printf(dcb, "Session %" PRIu64 "\n", print_session->ses_id);
    dcb_printf(dcb, "\tState:               %s\n", session_state(print_session->state));
    dcb_printf(dcb, "\tService:             %s\n", print_session->service->name);

    if (print_session->client_dcb && print_session->client_dcb->remote)
    {
        double idle = (mxs_clock() - print_session->client_dcb->last_read);
        idle = idle > 0 ? idle / 10.f : 0;
        dcb_printf(dcb,
                   "\tClient Address:          %s%s%s\n",
                   print_session->client_dcb->user ? print_session->client_dcb->user : "",
                   print_session->client_dcb->user ? "@" : "",
                   print_session->client_dcb->remote);
        dcb_printf(dcb,
                   "\tConnected:               %s\n",
                   asctime_r(localtime_r(&print_session->stats.connect, &result), buf));
        if (print_session->client_dcb->state == DCB_STATE_POLLING)
        {
            dcb_printf(dcb, "\tIdle:                %.0f seconds\n", idle);
        }
    }

    Session* session = static_cast<Session*>(print_session);

    for (const auto& f : session->get_filters())
    {
        dcb_printf(dcb, "\tFilter: %s\n", f.filter->name.c_str());
        f.filter->obj->diagnostics(f.instance, f.session, dcb);
    }
}

bool dListSessions_cb(DCB* dcb, void* data)
{
    if (dcb->dcb_role == DCB_ROLE_CLIENT_HANDLER)
    {
        DCB* out_dcb = (DCB*)data;
        MXS_SESSION* session = dcb->session;
        dcb_printf(out_dcb,
                   "%-16" PRIu64 " | %-15s | %-14s | %s\n",
                   session->ses_id,
                   session->client_dcb && session->client_dcb->remote ?
                   session->client_dcb->remote : "",
                   session->service && session->service->name ?
                   session->service->name : "",
                   session_state(session->state));
    }

    return true;
}
/**
 * List all sessions in tabular form to a DCB
 *
 * Designed to be called within a debugger session in order
 * to display all active sessions within the gateway
 *
 * @param dcb   The DCB to print to
 */
void dListSessions(DCB* dcb)
{
    dcb_printf(dcb, "-----------------+-----------------+----------------+--------------------------\n");
    dcb_printf(dcb, "Session          | Client          | Service        | State\n");
    dcb_printf(dcb, "-----------------+-----------------+----------------+--------------------------\n");

    dcb_foreach(dListSessions_cb, dcb);

    dcb_printf(dcb, "-----------------+-----------------+----------------+--------------------------\n\n");
}

/**
 * Convert a session state to a string representation
 *
 * @param state         The session state
 * @return A string representation of the session state
 */
const char* session_state(mxs_session_state_t state)
{
    switch (state)
    {
    case SESSION_STATE_ALLOC:
        return "Session Allocated";

    case SESSION_STATE_DUMMY:
        return "Dummy Session";

    case SESSION_STATE_READY:
        return "Session Ready";

    case SESSION_STATE_ROUTER_READY:
        return "Session ready for routing";

    case SESSION_STATE_LISTENER:
        return "Listener Session";

    case SESSION_STATE_LISTENER_STOPPED:
        return "Stopped Listener Session";

    case SESSION_STATE_STOPPING:
        return "Stopping session";

    case SESSION_STATE_TO_BE_FREED:
        return "Session to be freed";

    case SESSION_STATE_FREE:
        return "Freed session";

    default:
        return "Invalid State";
    }
}

/**
 * Create the filter chain for this session.
 *
 * Filters must be setup in reverse order, starting with the last
 * filter in the chain and working back towards the client connection
 * Each filter is passed the current session head of the filter chain
 * this head becomes the destination for the filter. The newly created
 * filter becomes the new head of the filter chain.
 *
 * @param       session         The session that requires the chain
 * @return      0 if filter creation fails
 */
static int session_setup_filters(MXS_SESSION* ses)
{
    Service* service = static_cast<Service*>(ses->service);
    Session* session = static_cast<Session*>(ses);
    return session->setup_filters(service);
}

/**
 * Entry point for the final element in the upstream filter, i.e. the writing
 * of the data to the client.
 *
 * Looks like a filter `clientReply`, but in this case both the instance and
 * the session argument will be a MXS_SESSION*.
 *
 * @param       instance        The "instance" data
 * @param       session         The session
 * @param       data            The buffer chain to write
 */
int session_reply(MXS_FILTER* instance, MXS_FILTER_SESSION* session, GWBUF* data)
{
    MXS_SESSION* the_session = (MXS_SESSION*)session;

    return the_session->client_dcb->func.write(the_session->client_dcb, data);
}

/**
 * Return the client connection address or name
 *
 * @param session       The session whose client address to return
 */
const char* session_get_remote(const MXS_SESSION* session)
{
    if (session && session->client_dcb)
    {
        return session->client_dcb->remote;
    }
    return NULL;
}

bool session_route_query(MXS_SESSION* session, GWBUF* buffer)
{
    mxb_assert(session);
    mxb_assert(session->head.routeQuery);
    mxb_assert(session->head.instance);
    mxb_assert(session->head.session);

    bool rv;

    if (session->head.routeQuery(session->head.instance, session->head.session, buffer) == 1)
    {
        rv = true;
    }
    else
    {
        rv = false;
    }

    // In case some filter has short-circuited the request processing we need
    // to deliver that now to the client.
    session_deliver_response(session);

    return rv;
}

bool session_route_reply(MXS_SESSION* session, GWBUF* buffer)
{
    mxb_assert(session);
    mxb_assert(session->tail.clientReply);
    mxb_assert(session->tail.instance);
    mxb_assert(session->tail.session);

    bool rv;

    if (session->tail.clientReply(session->tail.instance, session->tail.session, buffer) == 1)
    {
        rv = true;
    }
    else
    {
        rv = false;
    }

    return rv;
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
    return (session && session->client_dcb) ? session->client_dcb->user : NULL;
}

bool dcb_iter_cb(DCB* dcb, void* data)
{
    if (dcb->dcb_role == DCB_ROLE_CLIENT_HANDLER)
    {
        ResultSet* set = static_cast<ResultSet*>(data);
        MXS_SESSION* ses = dcb->session;
        char buf[20];
        snprintf(buf, sizeof(buf), "%p", ses);

        set->add_row({buf, ses->client_dcb->remote, ses->service->name, session_state(ses->state)});
    }

    return true;
}

/**
 * Return a resultset that has the current set of sessions in it
 *
 * @return A Result set
 */
/* Lint is not convinced that the new memory for data is always tracked
 * because it does not see what happens within the resultset_create function,
 * so we suppress the warning. In fact, the function call results in return
 * of the set structure which includes a pointer to data
 */
std::unique_ptr<ResultSet> sessionGetList()
{
    std::unique_ptr<ResultSet> set = ResultSet::create({"Session", "Client", "Service", "State"});
    dcb_foreach(dcb_iter_cb, set.get());
    return set;
}

mxs_session_trx_state_t session_get_trx_state(const MXS_SESSION* ses)
{
    return ses->trx_state;
}

mxs_session_trx_state_t session_set_trx_state(MXS_SESSION* ses, mxs_session_trx_state_t new_state)
{
    mxs_session_trx_state_t prev_state = ses->trx_state;

    ses->trx_state = new_state;

    return prev_state;
}

const char* session_trx_state_to_string(mxs_session_trx_state_t state)
{
    switch (state)
    {
    case SESSION_TRX_INACTIVE:
        return "SESSION_TRX_INACTIVE";

    case SESSION_TRX_ACTIVE:
        return "SESSION_TRX_ACTIVE";

    case SESSION_TRX_READ_ONLY:
        return "SESSION_TRX_READ_ONLY";

    case SESSION_TRX_READ_WRITE:
        return "SESSION_TRX_READ_WRITE";

    case SESSION_TRX_READ_ONLY_ENDING:
        return "SESSION_TRX_READ_ONLY_ENDING";

    case SESSION_TRX_READ_WRITE_ENDING:
        return "SESSION_TRX_READ_WRITE_ENDING";
    }

    MXS_ERROR("Unknown session_trx_state_t value: %d", (int)state);
    return "UNKNOWN";
}

static bool ses_find_id(DCB* dcb, void* data)
{
    void** params = (void**)data;
    MXS_SESSION** ses = (MXS_SESSION**)params[0];
    uint64_t* id = (uint64_t*)params[1];
    bool rval = true;

    if (dcb->session->ses_id == *id)
    {
        *ses = session_get_ref(dcb->session);
        rval = false;
    }

    return rval;
}

MXS_SESSION* session_get_by_id(uint64_t id)
{
    MXS_SESSION* session = NULL;
    void* params[] = {&session, &id};

    dcb_foreach(ses_find_id, params);

    return session;
}

MXS_SESSION* session_get_ref(MXS_SESSION* session)
{
    mxb::atomic::add(&session->refcount, 1);
    return session;
}

void session_put_ref(MXS_SESSION* session)
{
    if (session && session->state != SESSION_STATE_DUMMY)
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
    return mxb::atomic::add(&next_session_id, 1, mxb::atomic::RELAXED);
}

json_t* session_json_data(const Session* session, const char* host)
{
    json_t* data = json_object();

    /** ID must be a string */
    stringstream ss;
    ss << session->ses_id;

    /** ID and type */
    json_object_set_new(data, CN_ID, json_string(ss.str().c_str()));
    json_object_set_new(data, CN_TYPE, json_string(CN_SESSIONS));

    /** Relationships */
    json_t* rel = json_object();

    /** Service relationship (one-to-one) */
    json_t* services = mxs_json_relationship(host, MXS_JSON_API_SERVICES);
    mxs_json_add_relation(services, session->service->name, CN_SERVICES);
    json_object_set_new(rel, CN_SERVICES, services);

    /** Filter relationships (one-to-many) */
    auto filter_list = session->get_filters();

    if (!filter_list.empty())
    {
        json_t* filters = mxs_json_relationship(host, MXS_JSON_API_FILTERS);

        for (const auto& f : filter_list)
        {
            mxs_json_add_relation(filters, f.filter->name.c_str(), CN_FILTERS);
        }
        json_object_set_new(rel, CN_FILTERS, filters);
    }

    json_object_set_new(data, CN_RELATIONSHIPS, rel);

    /** Session attributes */
    json_t* attr = json_object();
    json_object_set_new(attr, "state", json_string(session_state(session->state)));

    if (session->client_dcb->user)
    {
        json_object_set_new(attr, CN_USER, json_string(session->client_dcb->user));
    }

    if (session->client_dcb->remote)
    {
        json_object_set_new(attr, "remote", json_string(session->client_dcb->remote));
    }

    struct tm result;
    char buf[60];

    asctime_r(localtime_r(&session->stats.connect, &result), buf);
    trim(buf);

    json_object_set_new(attr, "connected", json_string(buf));

    if (session->client_dcb->state == DCB_STATE_POLLING)
    {
        double idle = (mxs_clock() - session->client_dcb->last_read);
        idle = idle > 0 ? idle / 10.f : 0;
        json_object_set_new(attr, "idle", json_real(idle));
    }

    json_t* dcb_arr = json_array();
    const Session* pSession = static_cast<const Session*>(session);

    for (auto d : pSession->dcb_set())
    {
        json_array_append_new(dcb_arr, dcb_to_json(d));
    }

    json_object_set_new(attr, "connections", dcb_arr);

    json_object_set_new(data, CN_ATTRIBUTES, attr);
    json_object_set_new(data, CN_LINKS, mxs_json_self_link(host, CN_SESSIONS, ss.str().c_str()));

    return data;
}

json_t* session_to_json(const MXS_SESSION* session, const char* host)
{
    stringstream ss;
    ss << MXS_JSON_API_SESSIONS << session->ses_id;
    const Session* s = static_cast<const Session*>(session);
    return mxs_json_resource(host, ss.str().c_str(), session_json_data(s, host));
}

struct SessionListData
{
    json_t*     json;
    const char* host;
};

bool seslist_cb(DCB* dcb, void* data)
{
    if (dcb->dcb_role == DCB_ROLE_CLIENT_HANDLER)
    {
        SessionListData* d = (SessionListData*)data;
        Session* session = static_cast<Session*>(dcb->session);
        json_array_append_new(d->json, session_json_data(session, d->host));
    }

    return true;
}

json_t* session_list_to_json(const char* host)
{
    SessionListData data = {json_array(), host};
    dcb_foreach(seslist_cb, &data);
    return mxs_json_resource(host, MXS_JSON_API_SESSIONS, data.json);
}

void session_qualify_for_pool(MXS_SESSION* session)
{
    session->qualifies_for_pooling = true;
}

bool session_valid_for_pool(const MXS_SESSION* session)
{
    mxb_assert(session->state != SESSION_STATE_DUMMY);
    return session->qualifies_for_pooling;
}

MXS_SESSION* session_get_current()
{
    DCB* dcb = dcb_get_current();

    return dcb ? dcb->session : NULL;
}

uint64_t session_get_current_id()
{
    MXS_SESSION* session = session_get_current();

    return session ? session->ses_id : 0;
}

bool session_add_variable(MXS_SESSION* session,
                          const char*  name,
                          session_variable_handler_t handler,
                          void* context)
{
    Session* pSession = static_cast<Session*>(session);
    return pSession->add_variable(name, handler, context);
}

char* session_set_variable_value(MXS_SESSION* session,
                                 const char*  name_begin,
                                 const char*  name_end,
                                 const char*  value_begin,
                                 const char*  value_end)
{
    Session* pSession = static_cast<Session*>(session);
    return pSession->set_variable_value(name_begin, name_end, value_begin, value_end);
}

bool session_remove_variable(MXS_SESSION* session,
                             const char*  name,
                             void** context)
{
    Session* pSession = static_cast<Session*>(session);
    return pSession->remove_variable(name, context);
}

void session_set_response(MXS_SESSION* session, const MXS_UPSTREAM* up, GWBUF* buffer)
{
    // Valid arguments.
    mxb_assert(session && up && buffer);

    // Valid state. Only one filter may terminate the execution and exactly once.
    mxb_assert(!session->response.up.instance
               && !session->response.up.session
               && !session->response.buffer);

    session->response.up = *up;
    session->response.buffer = buffer;
}

/**
 * Delivers a provided response to the upstream filter that should
 * receive it.
 *
 * @param session  The session.
 */
static void session_deliver_response(MXS_SESSION* session)
{
    MXS_FILTER* filter_instance = session->response.up.instance;

    if (filter_instance)
    {
        MXS_FILTER_SESSION* filter_session = session->response.up.session;
        GWBUF* buffer = session->response.buffer;

        mxb_assert(filter_session);
        mxb_assert(buffer);

        session->response.up.clientReply(filter_instance, filter_session, buffer);

        session->response.up.instance = NULL;
        session->response.up.session = NULL;
        session->response.up.clientReply = NULL;
        session->response.buffer = NULL;
    }

    mxb_assert(!session->response.up.instance);
    mxb_assert(!session->response.up.session);
    mxb_assert(!session->response.up.clientReply);
    mxb_assert(!session->response.buffer);
}

void session_set_retain_last_statements(uint32_t n)
{
    retain_last_statements = n;
}

void session_set_dump_statements(session_dump_statements_t value)
{
    dump_statements = value;
}

session_dump_statements_t session_get_dump_statements()
{
    return dump_statements;
}

void session_retain_statement(MXS_SESSION* session, GWBUF* pBuffer)
{
    Session* pSession = static_cast<Session*>(session);
    pSession->retain_statement(pBuffer);
}

void session_dump_statements(MXS_SESSION* session)
{
    Session* pSession = static_cast<Session*>(session);
    pSession->dump_statements();
}

class DelayedRoutingTask
{
    DelayedRoutingTask(const DelayedRoutingTask&) = delete;
    DelayedRoutingTask& operator=(const DelayedRoutingTask&) = delete;

public:
    DelayedRoutingTask(MXS_SESSION* session, MXS_DOWNSTREAM down, GWBUF* buffer)
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

    void execute()
    {
        if (m_session->state == SESSION_STATE_ROUTER_READY)
        {
            GWBUF* buffer = m_buffer;
            m_buffer = NULL;

            if (m_down.routeQuery(m_down.instance, m_down.session, buffer) == 0)
            {
                // Routing failed, send a hangup to the client.
                poll_fake_hangup_event(m_session->client_dcb);
            }
        }
    }

private:
    MXS_SESSION*   m_session;
    MXS_DOWNSTREAM m_down;
    GWBUF*         m_buffer;
};

static bool delayed_routing_cb(Worker::Call::action_t action, DelayedRoutingTask* task)
{
    if (action == Worker::Call::EXECUTE)
    {
        task->execute();
    }

    delete task;
    return false;
}

bool session_delay_routing(MXS_SESSION* session, MXS_DOWNSTREAM down, GWBUF* buffer, int seconds)
{
    bool success = false;

    try
    {
        Worker* worker = Worker::get_current();
        mxb_assert(worker == session->client_dcb->poll.owner);
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

MXS_DOWNSTREAM router_as_downstream(MXS_SESSION* session)
{
    MXS_DOWNSTREAM head;
    head.instance = (MXS_FILTER*)session->service->router_instance;
    head.session = (MXS_FILTER_SESSION*)session->router_session;
    head.routeQuery = (DOWNSTREAMFUNC)session->service->router->routeQuery;
    return head;
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

Session::~Session()
{
    if (router_session)
    {
        service->router->freeSession(service->router_instance, router_session);
    }

    for (auto& f : m_filters)
    {
        f.filter->obj->closeSession(f.instance, f.session);
        f.filter->obj->freeSession(f.instance, f.session);
    }
}

void Session::dump_statements() const
{
    if (retain_last_statements)
    {
        int n = m_last_statements.size();

        uint64_t id = session_get_current_id();

        if ((id != 0) && (id != ses_id))
        {
            MXS_WARNING("Current session is %" PRIu64 ", yet statements are dumped for %" PRIu64 ". "
                                                                                                 "The session id in the subsequent dumped statements is the wrong one.",
                        id,
                        ses_id);
        }

        for (auto i = m_last_statements.rbegin(); i != m_last_statements.rend(); ++i)
        {
            int len = i->size();
            const char* pStmt = (char*)&i->front();

            if (id != 0)
            {
                MXS_NOTICE("Stmt %d: %.*s", n, len, pStmt);
            }
            else
            {
                // We are in a context where we do not have a current session, so we need to
                // log the session id ourselves.

                MXS_NOTICE("(%" PRIu64 ") Stmt %d: %.*s", ses_id, n, len, pStmt);
            }

            --n;
        }
    }
}

bool Session::setup_filters(Service* service)
{
    for (const auto& a : service->get_filters())
    {
        m_filters.emplace_back(a);
    }

    for (auto it = m_filters.rbegin(); it != m_filters.rend(); it++)
    {
        MXS_DOWNSTREAM* my_head = filter_apply(it->filter, this, &head);

        if (my_head == NULL)
        {
            MXS_ERROR("Failed to create filter '%s' for service '%s'.\n",
                      filter_def_get_name(it->filter.get()),
                      service->name);
            return false;
        }

        it->session = my_head->session;
        it->instance = my_head->instance;
        head = *my_head;
        MXS_FREE(my_head);
    }

    for (auto it = m_filters.begin(); it != m_filters.end(); it++)
    {
        MXS_UPSTREAM* my_tail = filter_upstream(it->filter, it->session, &tail);

        if (my_tail == NULL)
        {
            MXS_ERROR("Failed to create filter '%s' for service '%s'.",
                      filter_def_get_name(it->filter.get()),
                      service->name);
            return false;
        }

        /**
         * filter_upstream may simply return the 3 parameters if the filter has no
         * upstream entry point. So no need to copy the contents or free tail in this case.
         */
        if (my_tail != &tail)
        {
            tail = *my_tail;
            MXS_FREE(my_tail);
        }
    }

    return true;
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
    if (retain_last_statements)
    {
        size_t len = gwbuf_length(pBuffer);

        if (len > MYSQL_HEADER_LEN)
        {
            uint8_t header[MYSQL_HEADER_LEN + 1];
            uint8_t* pHeader = NULL;

            if (GWBUF_LENGTH(pBuffer) > MYSQL_HEADER_LEN)
            {
                pHeader = GWBUF_DATA(pBuffer);
            }
            else
            {
                gwbuf_copy_data(pBuffer, 0, MYSQL_HEADER_LEN + 1, header);
                pHeader = header;
            }

            if (MYSQL_GET_COMMAND(pHeader) == MXS_COM_QUERY)
            {
                mxb_assert(m_last_statements.size() <= retain_last_statements);

                if (m_last_statements.size() == retain_last_statements)
                {
                    m_last_statements.pop_back();
                }

                std::vector<uint8_t> stmt(len - MYSQL_HEADER_LEN - 1);
                gwbuf_copy_data(pBuffer, MYSQL_HEADER_LEN + 1, len - (MYSQL_HEADER_LEN + 1), &stmt.front());

                m_last_statements.push_front(stmt);
            }
        }
    }
}
