/*
 * Copyright (c) 2017 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl.
 *
 * Change Date: 2023-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * @file roundrobinrouter.c - Round-robin router load balancer
 *
 * This is an implementation of a simple query router that balances reads on a
 * query level. The router is configured with a set of slaves and optionally
 * a master. The router balances the client read queries over the set of slave
 * servers, sending write operations to the master. Session-operations are sent
 * to all slaves and the master. The read query balancing is done in round robin
 * style: in each session, the slave servers (and the master if inserted into the
 * slave list) take turns processing read queries.
 *
 * This router is intended to be a rather straightforward example on how to
 * program a module for MariaDB MaxScale. The router does not yet support all
 * SQL-commands and there are bound to be various limitations yet unknown. It
 * does work on basic reads and writes.
 *
 */

/* The log macros use this definition. */
#define MXS_MODULE_NAME "RoundRobinRouter"

#include <maxscale/ccdefs.hh>

#include <vector>
#include <iostream>
#include <string>
#include <iterator>

#include <maxbase/alloc.h>
#include <maxscale/buffer.hh>
#include <maxscale/dcb.hh>
#include <maxscale/modinfo.hh>
#include <maxscale/modulecmd.hh>
#include <maxscale/modutil.hh>
#include <maxscale/protocol/mysql.hh>
#include <maxscale/query_classifier.hh>
#include <maxscale/router.hh>

// #define DEBUG_RRROUTER
#undef DEBUG_RROUTER

#ifdef DEBUG_RRROUTER
#define RR_DEBUG(msg, ...) MXS_NOTICE(msg, ##__VA_ARGS__)
#else
#define RR_DEBUG(msg, ...)
#endif

/* This router handles different query types in a different manner. Some queries
 * require that a "write_backend" is set. */
const uint32_t q_route_to_rr = (QUERY_TYPE_LOCAL_READ | QUERY_TYPE_READ
                                | QUERY_TYPE_MASTER_READ | QUERY_TYPE_USERVAR_READ
                                | QUERY_TYPE_SYSVAR_READ | QUERY_TYPE_GSYSVAR_READ
                                | QUERY_TYPE_SHOW_DATABASES | QUERY_TYPE_SHOW_TABLES);

const uint32_t q_route_to_all = (QUERY_TYPE_SESSION_WRITE | QUERY_TYPE_USERVAR_WRITE
                                 | QUERY_TYPE_GSYSVAR_WRITE | QUERY_TYPE_ENABLE_AUTOCOMMIT
                                 | QUERY_TYPE_DISABLE_AUTOCOMMIT);

const uint32_t q_trx_begin = QUERY_TYPE_BEGIN_TRX;

const uint32_t q_trx_end = (QUERY_TYPE_ROLLBACK | QUERY_TYPE_COMMIT);

const uint32_t q_route_to_write = (QUERY_TYPE_WRITE | QUERY_TYPE_PREPARE_NAMED_STMT
                                   | QUERY_TYPE_PREPARE_STMT | QUERY_TYPE_EXEC_STMT
                                   | QUERY_TYPE_CREATE_TMP_TABLE | QUERY_TYPE_READ_TMP_TABLE);

const char MAX_BACKENDS[] = "max_backends";
const char WRITE_BACKEND[] = "write_backend";
const char PRINT_ON_ROUTING[] = "print_on_routing";
const char DUMMY[] = "dummy_setting";

/* Enum setting definition example */
static const MXS_ENUM_VALUE enum_example[] =
{
    {"two",  2},
    {"zero", 0},
    {NULL}      /* Last must be NULL */
};

using std::string;
using std::cout;

typedef std::vector<DCB*> DCB_VEC;

class RRRouter;

/* Every client connection has a corresponding session. */
class RRRouterSession : public mxs::RouterSession
{
public:

    // The API functions must be public
    RRRouterSession(RRRouter*, DCB_VEC&, DCB*, DCB*);
    ~RRRouterSession();
    void    close();
    int32_t routeQuery(GWBUF* buffer);
    void    clientReply(GWBUF* buffer, DCB* dcb);
    void    handleError(GWBUF* message, DCB* problem_dcb, mxs_error_action_t action, bool* succp);

private:
    bool         m_closed;              /* true when closeSession is called */
    DCB_VEC      m_backend_dcbs;        /* backends */
    DCB*         m_write_dcb;           /* write backend */
    DCB*         m_client_dcb;          /* client */
    unsigned int m_route_count;         /* how many packets have been routed */
    bool         m_on_transaction;      /* Is the session in transaction mode? */
    unsigned int m_replies_to_ignore;   /* Counts how many replies should be ignored. */
    RRRouter*    m_router;

    void decide_target(GWBUF* querybuf, DCB*& target, bool& route_to_all);
};

/* Each service using this router will have a router object instance. */
class RRRouter : public mxs::Router<RRRouter, RRRouterSession>
{
public:

    // The routing capabilities that this module requires. The getCapabilities entry point and the
    // capabilities given in the module declaration should be the same.
    static constexpr const uint64_t CAPABILITIES {RCAP_TYPE_CONTIGUOUS_INPUT | RCAP_TYPE_RESULTSET_OUTPUT};

    ~RRRouter();
    static RRRouter* create(SERVICE* pService, MXS_CONFIG_PARAMETER* params);
    RRRouterSession* newSession(MXS_SESSION* session);
    void             diagnostics(DCB* dcb) const;
    json_t*          diagnostics_json() const;

    uint64_t getCapabilities()
    {
        return CAPABILITIES;
    }

private:
    friend class RRRouterSession;

    SERVICE* m_service;             /* Service this router is part of */
    /* Router settings */
    unsigned int m_max_backends;    /* How many backend servers to use */
    SERVER*      m_write_server;    /* Where to send write etc. "unsafe" queries */
    bool         m_print_on_routing;/* Print a message on every packet routed? */
    uint64_t     m_example_enum;    /* Not used */

    /* Methods */
    RRRouter(SERVICE* service);

    /* Statistics, written to by multiple threads */
    std::atomic<uint64_t> m_routing_s;      /* Routing success */
    std::atomic<uint64_t> m_routing_f;      /* Routing fail */
    std::atomic<uint64_t> m_routing_c;      /* Client packets routed */
};

/**
 * Constructs a new router instance, called by the static `create` method.
 */
RRRouter::RRRouter(SERVICE* service)
    : mxs::Router<RRRouter, RRRouterSession>(service)
    , m_service(service)
    , m_routing_s(0)
    , m_routing_f(0)
    , m_routing_c(0)
{
    RR_DEBUG("Creating instance.");
    /* Read options specific to round robin router. */
    const MXS_CONFIG_PARAMETER& params = service->svc_config_param;
    m_max_backends = params.get_integer(MAX_BACKENDS);
    m_write_server = params.get_server(WRITE_BACKEND);
    m_print_on_routing = params.get_bool(PRINT_ON_ROUTING);
    m_example_enum = params.get_enum(DUMMY, enum_example);

    RR_DEBUG("Settings read:");
    RR_DEBUG("'%s': %d", MAX_BACKENDS, m_max_backends);
    RR_DEBUG("'%s': %p", WRITE_BACKEND, m_write_server);
    RR_DEBUG("'%s': %d", PRINT_ON_ROUTING, m_print_on_routing);
    RR_DEBUG("'%s': %lu", DUMMY, m_example_enum);
}

/**
 * Resources can be freed in the router destructor
 */
RRRouter::~RRRouter()
{
    RR_DEBUG("Deleting router instance.");
    RR_DEBUG("Queries routed successfully: %lu", m_routing_s.load());
    RR_DEBUG("Failed routing attempts: %lu", m_routing_f.load());
    RR_DEBUG("Client replies: %lu", m_routing_c.load());
}

/**
 * @brief Create a new router session for this router instance (API).
 *
 * Connect a client session to the router instance and return a router session.
 * The router session stores all client specific data required by the router.
 *
 * @param session   The MaxScale session (generic client connection data)
 *
 * @return          Client specific data for this router
 */
RRRouterSession* RRRouter::newSession(MXS_SESSION* session)
{
    DCB_VEC backends;
    DCB* write_dcb = NULL;
    RRRouterSession* rses = NULL;
    try
    {
        /* Try to connect to as many backends as required. */
        SERVER_REF* sref;
        for (sref = m_service->dbref; sref != NULL; sref = sref->next)
        {
            if (server_ref_is_active(sref) && (backends.size() < m_max_backends))
            {
                /* Connect to server */
                DCB* conn = dcb_connect(sref->server, session, sref->server->protocol().c_str());
                if (conn)
                {
                    /* Success */
                    atomic_add(&sref->connections, 1);
                    conn->m_service = session->service;
                    backends.push_back(conn);
                }   /* Any error by dcb_connect is reported by the function itself */
            }
        }
        if (m_write_server)
        {
            /* Connect to write backend server. This is not essential.  */
            write_dcb = dcb_connect(m_write_server, session, m_write_server->protocol().c_str());
            if (write_dcb)
            {
                /* Success */
                write_dcb->m_service = session->service;
            }
        }
        if (backends.size() < 1)
        {
            MXS_ERROR("Session creation failed, could not connect to any "
                      "read backends.");
        }
        else
        {
            rses = new RRRouterSession(this, backends, write_dcb, session->client_dcb);
            RR_DEBUG("Session with %lu connections created.",
                     backends.size() + (write_dcb ? 1 : 0));
        }
    }
    catch (const std::exception& x)
    {
        MXS_ERROR("Caught exception: %s", x.what());
        /* Close any connections already made */
        for (unsigned int i = 0; i < backends.size(); i++)
        {
            DCB* dcb = backends[i];
            dcb_close(dcb);
            atomic_add(&(m_service->dbref->connections), -1);
        }
        backends.clear();
        if (write_dcb)
        {
            dcb_close(write_dcb);
        }
    }
    return rses;
}

/**
 * @brief Create an instance of the router (API).
 *
 * Create an instance of the round robin router. One instance of the router is
 * created for each service that is defined in the configuration as using this
 * router. One instance of the router will handle multiple connections
 * (router sessions).
 *
 * @param service   The service this router is being created for
 * @param options   The options for this query router
 * @return          NULL in failure, pointer to router in success.
 */
RRRouter* RRRouter::create(SERVICE* pService, MXS_CONFIG_PARAMETER* params)
{
    return new(std::nothrow) RRRouter(pService);
}

/**
 * @brief Diagnostics routine (API)
 *
 * Print router statistics to the DCB passed in. This is usually called by the
 * MaxInfo or MaxAdmin modules.
 *
 * @param   instance    The router instance
 * @param   dcb         The DCB for diagnostic output
 */
void RRRouter::diagnostics(DCB* dcb) const
{
    dcb_printf(dcb, "\t\tQueries routed successfully: %lu\n", m_routing_s.load());
    dcb_printf(dcb, "\t\tFailed routing attempts:     %lu\n", m_routing_f.load());
    dcb_printf(dcb, "\t\tClient replies routed:       %lu\n", m_routing_c.load());
}

/**
 * @brief Diagnostics routine (API)
 *
 * Print router statistics to the DCB passed in. This is usually called by the
 * MaxInfo or MaxAdmin modules.
 *
 * @param   instance    The router instance
 * @param   dcb         The DCB for diagnostic output
 */
json_t* RRRouter::diagnostics_json() const
{
    json_t* rval = json_object();

    json_object_set_new(rval, "queries_ok", json_integer(m_routing_s.load()));
    json_object_set_new(rval, "queries_failed", json_integer(m_routing_f.load()));
    json_object_set_new(rval, "replies", json_integer(m_routing_c.load()));

    return rval;
}

/**
 * @brief Route a packet (API)
 *
 * The routeQuery function receives a packet and makes the routing decision
 * based on the contents of the router instance, router session and the query
 * itself. It then sends the query to the target backend(s).
 *
 * @param instance       Router instance
 * @param session        Router session associated with the client
 * @param buffer       Buffer containing the query (or command)
 * @return 1 on success, 0 on error
 */
int RRRouterSession::routeQuery(GWBUF* querybuf)
{
    int rval = 0;
    const bool print = m_router->m_print_on_routing;
    DCB* target = NULL;
    bool route_to_all = false;

    if (!m_closed)
    {
        decide_target(querybuf, target, route_to_all);
    }

    /* Target selection done, write to dcb. */
    if (target)
    {
        /* We have one target backend */
        if (print)
        {
            MXS_NOTICE("Routing statement of length %du  to backend '%s'.",
                       gwbuf_length(querybuf),
                       target->m_server->name());
        }
        /* Do not use dcb_write() to output to a dcb. dcb_write() is used only
         * for raw write in the procol modules. */
        rval = target->m_func.write(target, querybuf);
        /* After write, the buffer points to non-existing data. */
        querybuf = NULL;
    }
    else if (route_to_all)
    {
        int n_targets = m_backend_dcbs.size() + (m_write_dcb ? 1 : 0);
        if (print)
        {
            MXS_NOTICE("Routing statement of length %du to %d backends.",
                       gwbuf_length(querybuf),
                       n_targets);
        }
        int route_success = 0;
        for (unsigned int i = 0; i < m_backend_dcbs.size(); i++)
        {
            DCB* dcb = m_backend_dcbs[i];
            /* Need to clone the buffer since write consumes it */
            GWBUF* copy = gwbuf_clone(querybuf);
            if (copy)
            {
                route_success += dcb->m_func.write(dcb, copy);
            }
        }
        if (m_write_dcb)
        {
            GWBUF* copy = gwbuf_clone(querybuf);
            if (copy)
            {
                route_success += m_write_dcb->m_func.write(m_write_dcb, copy);
            }
        }
        m_replies_to_ignore += route_success - 1;
        rval = (route_success == n_targets) ? 1 : 0;
        gwbuf_free(querybuf);
    }
    else
    {
        MXS_ERROR("Could not find a valid routing backend. Either the "
                  "'%s' is not set or the command is not recognized.",
                  WRITE_BACKEND);
        gwbuf_free(querybuf);
    }
    if (rval == 1)
    {
        /* Non-atomic update of shared data, but contents are non-essential */
        m_router->m_routing_s++;
    }
    else
    {
        m_router->m_routing_f++;
    }
    return rval;
}

/**
 * @brief Client Reply routine (API)
 *
 * This routine receives a packet from a backend server meant for the client.
 * Often, there is little logic needed and the packet can just be forwarded to
 * the next element in the processing chain.
 *
 * @param   queue       The GWBUF with reply data
 * @param   backend_dcb The backend DCB (data source)
 */
void RRRouterSession::clientReply(GWBUF* buf, DCB* backend_dcb)
{
    if (m_replies_to_ignore > 0)
    {
        /* In this case MaxScale cloned the message to many backends but the client
         * expects just one reply. Assume that client does not send next query until
         * previous has been answered.
         */
        m_replies_to_ignore--;
        gwbuf_free(buf);
        return;
    }

    RouterSession::clientReply(buf, backend_dcb);

    m_router->m_routing_c++;
    if (m_router->m_print_on_routing)
    {
        MXS_NOTICE("Replied to client.\n");
    }
}

/**
 * Error Handler routine (API)
 *
 * This routine will handle errors that occurred with the session. This function
 * is called if routeQuery() returns 0 instead of 1. The client or a backend
 * unexpectedly closing a connection also triggers this routine.
 *
 * @param       message         The error message to reply
 * @param       problem_dcb     The DCB related to the error
 * @param       action          The action: ERRACT_NEW_CONNECTION or ERRACT_REPLY_CLIENT
 * @param       succp           Output result of action, true if router can continue
 */
void RRRouterSession::handleError(GWBUF* message,
                                  DCB* problem_dcb,
                                  mxs_error_action_t action,
                                  bool* succp)
{
    MXS_SESSION* session = problem_dcb->m_session;
    DCB* client_dcb = session->client_dcb;
    MXS_SESSION::State sesstate = session->state();

    /* If the erroneous dcb is a client handler, close it. Setting succp to
     * false will cause the entire attached session to be closed.
     */
    if (problem_dcb->m_role == DCB::Role::CLIENT)
    {
        dcb_close(problem_dcb);
        *succp = false;
    }
    else
    {
        switch (action)
        {
        case ERRACT_REPLY_CLIENT:
            {
                /* React to failed authentication, send message to client */
                if (sesstate == MXS_SESSION::State::STARTED)
                {
                    /* Send error report to client */
                    GWBUF* copy = gwbuf_clone(message);
                    if (copy)
                    {
                        client_dcb->m_func.write(client_dcb, copy);
                    }
                }
                *succp = false;
            }
            break;

        case ERRACT_NEW_CONNECTION:
            {
                /* React to a failed backend */
                if (problem_dcb->m_role == DCB::Role::BACKEND)
                {
                    if (problem_dcb == m_write_dcb)
                    {
                        dcb_close(m_write_dcb);
                        m_write_dcb = NULL;
                    }
                    else
                    {
                        /* Find dcb in the list of backends */
                        DCB_VEC::iterator iter = m_backend_dcbs.begin();
                        while (iter != m_backend_dcbs.end())
                        {
                            if (*iter == problem_dcb)
                            {
                                dcb_close(*iter);
                                m_backend_dcbs.erase(iter);
                                break;
                            }
                        }
                    }

                    /* If there is still backends remaining, return true since
                     * router can still function.
                     */
                    *succp = (m_backend_dcbs.size() > 0) ? true : false;
                }
            }
            break;

        default:
            mxb_assert(!true);
            *succp = false;
            break;
        }
    }
}

RRRouterSession::RRRouterSession(RRRouter* router, DCB_VEC& backends, DCB* write, DCB* client)
    : RouterSession(client->session)
    , m_closed(false)
    , m_route_count(0)
    , m_on_transaction(false)
    , m_replies_to_ignore(0)
    , m_router(router)
{
    m_backend_dcbs = backends;
    m_write_dcb = write;
    m_client_dcb = client;
}

RRRouterSession::~RRRouterSession()
{
    /* Shouldn't happen. */
    mxb_assert(m_closed);
}

/**
 * @brief Close an existing router session for this router instance (API).
 *
 * Close a client session attached to the router instance. This function should
 * close connections and release other resources allocated in "newSession" or
 * otherwise held by the router session. This function should NOT free the
 * session object itself.
 */
void RRRouterSession::close()
{
    if (!m_closed)
    {
        /**
         * Mark router session as closed. @c m_closed is checked at the start
         * of most API functions to quickly stop the processing of closed sessions.
         */
        m_closed = true;
        for (unsigned int i = 0; i < m_backend_dcbs.size(); i++)
        {
            DCB* dcb = m_backend_dcbs[i];
            SERVER_REF* sref = dcb->m_service->dbref;
            while (sref && (sref->server != dcb->m_server))
            {
                sref = sref->next;
            }
            if (sref)
            {
                atomic_add(&(sref->connections), -1);
            }
            dcb_close(dcb);
        }
        int closed_conns = m_backend_dcbs.size();
        m_backend_dcbs.clear();
        if (m_write_dcb)
        {
            dcb_close(m_write_dcb);
            m_write_dcb = NULL;
            closed_conns++;
        }
        RR_DEBUG("Session with %d connections closed.", closed_conns);
    }
}

void RRRouterSession::decide_target(GWBUF* querybuf, DCB*& target, bool& route_to_all)
{
    /* Extract the command type from the SQL-buffer */
    mxs_mysql_cmd_t cmd_type = MYSQL_GET_COMMAND(GWBUF_DATA(querybuf));
    /* The "query_types" is only really valid for query-commands but let's use
     * it here for all command types.
     */
    uint32_t query_types = 0;

    switch (cmd_type)
    {
    case MXS_COM_QUERY:
        {
            /* Use the inbuilt query_classifier to get information about
             * the query. The default qc works with mySQL-queries.
             */
            query_types = qc_get_type_mask(querybuf);

#ifdef DEBUG_RRROUTER
            char* zSql_query = NULL;
            int length = 0;
            modutil_extract_SQL(querybuf, &zSql_query, &length);
            string sql_query(zSql_query, length);
            RR_DEBUG("QUERY: %s", sql_query.c_str());
#endif
        }
        break;

    case MXS_COM_INIT_DB:
        query_types = q_route_to_all;
        RR_DEBUG("MYSQL_COM_INIT_DB");
        break;

    case MXS_COM_QUIT:
        query_types = q_route_to_all;
        RR_DEBUG("MYSQL_COM_QUIT");
        break;

    case MXS_COM_FIELD_LIST:
        query_types = q_route_to_rr;
        RR_DEBUG("MYSQL_COM_FIELD_LIST");
        break;

    default:
        /*
         * TODO: Add support for other commands if needed.
         * This error message will only print the number of the cmd.
         */
        MXS_ERROR("Received unexpected sql command type: '%d'.", cmd_type);
        break;
    }

    if ((query_types & q_route_to_write) != 0)
    {
        target = m_write_dcb;
    }
    else
    {
        /* This is not yet sufficient for handling transactions. */
        if ((query_types & q_trx_begin) != 0)
        {
            m_on_transaction = true;
        }
        if (m_on_transaction)
        {
            /* If a transaction is going on, route all to write backend */
            target = m_write_dcb;
        }
        if ((query_types & q_trx_end) != 0)
        {
            m_on_transaction = false;
        }

        if (!target && ((query_types & q_route_to_rr) != 0))
        {
            /* Round robin backend. */
            unsigned int index = (m_route_count++) % m_backend_dcbs.size();
            target = m_backend_dcbs[index];
        }
        /* Some commands and queries are routed to all backends. */
        else if (!target && ((query_types & q_route_to_all) != 0))
        {
            route_to_all = true;
        }
    }
}

/* The next two entry points are optional. */

/**
 * Make any initializations required by the router module as a whole and not
 * specific to any individual router instance.
 *
 * @return 0 on success
 */
static int process_init()
{
    RR_DEBUG("Module loaded.");
    return 0;
}

/**
 * Undo module initializations.
 */
static void process_finish()
{
    RR_DEBUG("Module unloaded.");
}

static modulecmd_arg_type_t custom_cmd_args[] =
{
    {MODULECMD_ARG_STRING,                             "Example string"                    },
    {(MODULECMD_ARG_BOOLEAN | MODULECMD_ARG_OPTIONAL), "This is an optional bool parameter"}
};

/**
 * A function executed as a custom module command through MaxAdmin
 * @param argv The arguments
 */
bool custom_cmd_example(const MODULECMD_ARG* argv, json_t** output)
{
    cout << MXS_MODULE_NAME << " wishes the Admin a good day.\n";
    int n_args = argv->argc;
    cout << "The module got " << n_args << " arguments.\n";
    for (int i = 0; i < n_args; i++)
    {
        arg_node node = argv->argv[i];
        string type_str;
        string val_str;
        switch (MODULECMD_GET_TYPE(&node.type))
        {
        case MODULECMD_ARG_STRING:
            {
                type_str = "string";
                val_str.assign(node.value.string);
            }
            break;

        case MODULECMD_ARG_BOOLEAN:
            {
                type_str = "boolean";
                val_str.assign((node.value.boolean) ? "true" : "false");
            }
            break;

        default:
            {
                type_str = "other";
                val_str.assign("unknown");
            }
            break;
        }
        cout << "Argument " << i << ": type '" << type_str << "' value '" << val_str
             << "'\n";
    }
    return true;
}

/*
 * This is called by the module loader during MaxScale startup. A module
 * description, including entrypoints and allowed configuration parameters,
 * is returned. This function must be exported.
 */
extern "C" MXS_MODULE* MXS_CREATE_MODULE();
MXS_MODULE* MXS_CREATE_MODULE()
{
    /* Register a custom command */
    if (!modulecmd_register_command("rrrouter",
                                    "test_command",
                                    MODULECMD_TYPE_ACTIVE,
                                    custom_cmd_example,
                                    2,
                                    custom_cmd_args,
                                    "This is the command description"))
    {
        MXS_ERROR("Module command registration failed.");
    }

    static MXS_MODULE moduleObject =
    {
        MXS_MODULE_API_ROUTER,          /* Module type */
        MXS_MODULE_BETA_RELEASE,        /* Release status */
        MXS_ROUTER_VERSION,             /* Implemented module API version */
        "A simple round robin router",  /* Description */
        "V1.1.0",                       /* Module version */
        RRRouter::CAPABILITIES,
        &RRRouter::s_object,
        process_init,                   /* Process init, can be null */
        process_finish,                 /* Process finish, can be null */
        NULL,                           /* Thread init */
        NULL,                           /* Thread finish */
        {
            /* Next is an array of MODULE_PARAM structs, max 64 items. These define all
             * the possible parameters that this module accepts. This is required
             * since the module loader also parses the configuration file for the module.
             * Any unrecognised parameters in the config file are discarded.
             *
             * Note that many common parameters, such as backend servers, are
             * already set to the upper level "service"-object.
             */
            {                           /* For simple types, only 3 of the 5 struct fields need to be
                                         * defined. */
                MAX_BACKENDS,           /* Setting identifier in maxscale.cnf */
                MXS_MODULE_PARAM_INT,   /* Setting type */
                "0"                     /* Default value */
            },
            {PRINT_ON_ROUTING,        MXS_MODULE_PARAM_BOOL,    "false"},
            {WRITE_BACKEND,           MXS_MODULE_PARAM_SERVER,  NULL   },
            {   /* Enum types require an array with allowed values. */
                DUMMY,
                MXS_MODULE_PARAM_ENUM,
                "the_answer",
                MXS_MODULE_OPT_NONE,
                enum_example
            },
            {MXS_END_MODULE_PARAMS}
        }
    };
    return &moduleObject;
}
