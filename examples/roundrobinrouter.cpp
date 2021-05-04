/*
 * Copyright (c) 2017 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl.
 *
 * Change Date: 2025-04-28
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
#include <maxscale/protocol/mariadb/mysql.hh>
#include <maxscale/protocol/mariadb/query_classifier.hh>
#include <maxscale/router.hh>

// #define DEBUG_RRROUTER
#undef DEBUG_RROUTER

#ifdef DEBUG_RRROUTER
#define RR_DEBUG(msg, ...) MXS_NOTICE(msg, ##__VA_ARGS__)
#else
#define RR_DEBUG(msg, ...)
#endif

namespace
{

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


namespace cfg = mxs::config;

cfg::Specification s_spec(MXS_MODULE_NAME, cfg::Specification::ROUTER);

cfg::ParamCount s_max_backends(
    &s_spec, "max_backends", "Maximum number of backends to use",
    0);

cfg::ParamBool s_print_on_routing(
    &s_spec, "print_on_routing", "Print messages when routing queries",
    false);

cfg::ParamTarget s_write_backend(
    &s_spec, "write_backend", "Target used for writes");

cfg::ParamEnum<uint64_t> s_dummy(
    &s_spec, "dummy_setting", "A parameter that takes an enumeration",
    {
        {2, "two"},
        {0, "zero"},
    }, 2);
}

using std::string;
using std::cout;

typedef std::vector<BackendDCB*> DCB_VEC;

class RRRouter;

/* Every client connection has a corresponding session. */
class RRRouterSession : public mxs::RouterSession
{
public:

    // The API functions must be public
    RRRouterSession(RRRouter*, const mxs::Endpoints&, mxs::Endpoint*, MXS_SESSION*);
    ~RRRouterSession();
    int32_t routeQuery(GWBUF* buffer);
    int32_t clientReply(GWBUF* buffer, const mxs::ReplyRoute& down, const mxs::Reply& reply);
    bool    handleError(mxs::ErrorType type, GWBUF* message, mxs::Endpoint* down, const mxs::Reply& reply);

private:
    bool         m_closed;              /* true when closeSession is called */
    unsigned int m_route_count;         /* how many packets have been routed */
    bool         m_on_transaction;      /* Is the session in transaction mode? */
    unsigned int m_replies_to_ignore;   /* Counts how many replies should be ignored. */
    RRRouter*    m_router;

    mxs::Endpoints m_backends;
    mxs::Endpoint* m_write_backend;
    MXS_SESSION*   m_session;

    void decide_target(GWBUF* querybuf, mxs::Endpoint*& target, bool& route_to_all);
};

struct Config : public mxs::config::Configuration
{
    Config(const char* name);

    int64_t      max_backends;      /* How many backend servers to use */
    mxs::Target* write_server;      /* Where to send write etc. "unsafe" queries */
    bool         print_on_routing;  /* Print a message on every packet routed? */
    uint64_t     example_enum;      /* Not used */
};

/* Each service using this router will have a router object instance. */
class RRRouter : public mxs::Router
{
public:

    // The routing capabilities that this module requires. The getCapabilities entry point and the
    // capabilities given in the module declaration should be the same.
    static constexpr const uint64_t CAPABILITIES {RCAP_TYPE_STMT_INPUT | RCAP_TYPE_RESULTSET_OUTPUT};

    ~RRRouter();
    static RRRouter*    create(SERVICE* pService);
    mxs::RouterSession* newSession(MXS_SESSION* session, const mxs::Endpoints& endpoints);
    json_t*             diagnostics() const;

    uint64_t getCapabilities() const
    {
        return CAPABILITIES;
    }

    mxs::config::Configuration& getConfiguration()
    {
        return m_config;
    }

private:
    friend class RRRouterSession;

    SERVICE* m_service;             /* Service this router is part of */
    Config   m_config;

    /* Methods */
    RRRouter(SERVICE* service);

    /* Statistics, written to by multiple threads */
    std::atomic<uint64_t> m_routing_s;      /* Routing success */
    std::atomic<uint64_t> m_routing_f;      /* Routing fail */
    std::atomic<uint64_t> m_routing_c;      /* Client packets routed */
};

Config::Config(const char* name)
    : mxs::config::Configuration(name, &s_spec)
{
    add_native(&Config::max_backends, &s_max_backends);
    add_native(&Config::write_server, &s_write_backend);
    add_native(&Config::print_on_routing, &s_print_on_routing);
    add_native(&Config::example_enum, &s_dummy);
}

/**
 * Constructs a new router instance, called by the static `create` method.
 */
RRRouter::RRRouter(SERVICE* service)
    : m_service(service)
    , m_config(service->name())
    , m_routing_s(0)
    , m_routing_f(0)
    , m_routing_c(0)
{
    RR_DEBUG("Creating instance.");

    RR_DEBUG("Settings read:");
    RR_DEBUG("'%s': %d", MAX_BACKENDS, m_config.max_backends);
    RR_DEBUG("'%s': %p", WRITE_BACKEND, m_config.write_server);
    RR_DEBUG("'%s': %d", PRINT_ON_ROUTING, m_config.print_on_routing);
    RR_DEBUG("'%s': %lu", DUMMY, m_config.example_enum);
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
 * @param session    The MaxScale session (generic client connection data)
 * @param endspoints The routing endpoints that this session should use
 *
 * @return          Client specific data for this router
 */
mxs::RouterSession* RRRouter::newSession(MXS_SESSION* session, const mxs::Endpoints& endpoints)
{
    mxs::Endpoint* write_backend = nullptr;
    RRRouterSession* rses = NULL;
    int num_connections = 0;

    for (auto e : endpoints)
    {
        if (e->target() == m_config.write_server)
        {
            write_backend = e;
        }

        if (e->connect())
        {
            ++num_connections;
        }
    }

    if (num_connections > 0)
    {
        rses = new RRRouterSession(this, endpoints, write_backend, session);
        RR_DEBUG("Session with %lu connections created.", num_connections);
    }
    else
    {
        MXS_ERROR("Session creation failed, could not connect to any read backends.");
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
 *
 * @return          NULL in failure, pointer to router in success.
 */
RRRouter* RRRouter::create(SERVICE* pService)
{
    return new(std::nothrow) RRRouter(pService);
}

/**
 * @brief Diagnostics routine (API)
 *
 * Print router statistics to JSON. This is called by the REST-api.
 *
 * @param   instance    The router instance
 * @param   dcb         The DCB for diagnostic output
 */
json_t* RRRouter::diagnostics() const
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
    const bool print = m_router->m_config.print_on_routing;
    mxs::Endpoint* target = nullptr;
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
                       gwbuf_length(querybuf), target->target()->name());
        }

        rval = target->routeQuery(querybuf);
    }
    else if (route_to_all)
    {
        if (print)
        {
            MXS_NOTICE("Routing statement of length %du to %lu backends.",
                       gwbuf_length(querybuf), m_backends.size());
        }

        int n_targets = 0;
        int route_success = 0;

        for (auto b : m_backends)
        {
            if (b->is_open())
            {
                ++n_targets;

                if (b->routeQuery(gwbuf_clone(querybuf)))
                {
                    ++route_success;
                }
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
                  s_write_backend.name().c_str());
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
int32_t RRRouterSession::clientReply(GWBUF* buf, const mxs::ReplyRoute& down, const mxs::Reply& reply)
{
    if (m_replies_to_ignore > 0)
    {
        /* In this case MaxScale cloned the message to many backends but the client
         * expects just one reply. Assume that client does not send next query until
         * previous has been answered.
         */
        m_replies_to_ignore--;
        gwbuf_free(buf);
        return 1;
    }

    int32_t rc = RouterSession::clientReply(buf, down, reply);

    m_router->m_routing_c++;
    if (m_router->m_config.print_on_routing)
    {
        MXS_NOTICE("Replied to client.\n");
    }

    return rc;
}

bool RRRouterSession::handleError(mxs::ErrorType type,
                                  GWBUF* message,
                                  mxs::Endpoint* down,
                                  const mxs::Reply& reply)
{
    down->close();
    return std::any_of(m_backends.begin(), m_backends.end(), std::mem_fn(&mxs::Endpoint::is_open));
}

RRRouterSession::RRRouterSession(RRRouter* router, const mxs::Endpoints& backends,
                                 mxs::Endpoint* write_backend, MXS_SESSION* session)
    : RouterSession(session)
    , m_closed(false)
    , m_route_count(0)
    , m_on_transaction(false)
    , m_replies_to_ignore(0)
    , m_router(router)
    , m_backends(backends)
    , m_write_backend(write_backend)
    , m_session(session)
{
}

RRRouterSession::~RRRouterSession()
{
    if (!m_closed)
    {
        /**
         * Mark router session as closed. @c m_closed is checked at the start
         * of most API functions to quickly stop the processing of closed sessions.
         */
        m_closed = true;

        for (auto b : m_backends)
        {
            if (b->is_open())
            {
                b->close();
            }
        }

        RR_DEBUG("Session with %d connections closed.", closed_conns);
    }
    /* Shouldn't happen. */
    mxb_assert(m_closed);
}

void RRRouterSession::decide_target(GWBUF* querybuf, mxs::Endpoint*& target, bool& route_to_all)
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
        target = m_write_backend;
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
            target = m_write_backend;
        }
        if ((query_types & q_trx_end) != 0)
        {
            m_on_transaction = false;
        }

        if (!target && ((query_types & q_route_to_rr) != 0))
        {
            std::vector<mxs::Endpoint*> candidates;

            for (auto e : m_backends)
            {
                if (e->is_open() && e != m_write_backend)
                {
                    candidates.push_back(e);
                }
            }

            if (!candidates.empty())
            {
                target = candidates[m_route_count++ % candidates.size()];
            }
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
    if (!modulecmd_register_command("roundrobinrouter",
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
        mxs::MODULE_INFO_VERSION,
        MXS_MODULE_NAME,
        mxs::ModuleType::ROUTER,        /* Module type */
        mxs::ModuleStatus::BETA,        /* Release status */
        MXS_ROUTER_VERSION,             /* Implemented module API version */
        "A simple round robin router",  /* Description */
        "V1.1.0",                       /* Module version */
        RRRouter::CAPABILITIES,
        &mxs::RouterApi<RRRouter>::s_api,
        process_init,                   /* Process init, can be null */
        process_finish,                 /* Process finish, can be null */
        NULL,                           /* Thread init */
        NULL,                           /* Thread finish */
        {
            {MXS_END_MODULE_PARAMS}
        },
        &s_spec
    };
    return &moduleObject;
}
