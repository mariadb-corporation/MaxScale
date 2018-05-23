/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2020-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * @file avro.c - Avro router, allows MaxScale to act as an intermediary for
 * MySQL replication binlog files and AVRO binary files
 */

#include "avrorouter.hh"

#include <avro/errors.h>
#include <ctype.h>
#include <ini.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <glob.h>
#include <ini.h>
#include <sys/stat.h>
#include <avro/errors.h>
#include <maxscale/alloc.h>
#include <maxscale/atomic.h>
#include <maxscale/dcb.h>
#include <maxscale/log_manager.h>
#include <maxscale/modulecmd.h>
#include <maxscale/paths.h>
#include <maxscale/protocol/mysql.h>
#include <maxscale/random_jkiss.h>
#include <maxscale/router.h>
#include <maxscale/server.h>
#include <maxscale/service.h>
#include <maxscale/spinlock.h>
#include <maxscale/utils.h>
#include <maxscale/routingworker.h>
#include <maxscale/worker.hh>
#include <binlog_common.h>

using namespace maxscale;

#ifndef BINLOG_NAMEFMT
#define BINLOG_NAMEFMT      "%s.%06d"
#endif

/** For detection of CREATE/ALTER TABLE statements */
static const char* create_table_regex =
    "(?i)create[a-z0-9[:space:]_]+table";
static const char* alter_table_regex =
    "(?i)alter[[:space:]]+table";

/* The router entry points */
static MXS_ROUTER *createInstance(SERVICE *service, char **options);
static MXS_ROUTER_SESSION *newSession(MXS_ROUTER *instance, MXS_SESSION *session);
static void closeSession(MXS_ROUTER *instance, MXS_ROUTER_SESSION *router_session);
static void freeSession(MXS_ROUTER *instance, MXS_ROUTER_SESSION *router_session);
static int routeQuery(MXS_ROUTER *instance, MXS_ROUTER_SESSION *router_session, GWBUF *queue);
static void diagnostics(MXS_ROUTER *instance, DCB *dcb);
static json_t* diagnostics_json(const MXS_ROUTER *instance);
static void clientReply(MXS_ROUTER *instance, MXS_ROUTER_SESSION *router_session, GWBUF *queue,
                        DCB *backend_dcb);
static void errorReply(MXS_ROUTER *instance, MXS_ROUTER_SESSION *router_session, GWBUF *message,
                       DCB *backend_dcb, mxs_error_action_t action, bool *succp);
static uint64_t getCapabilities(MXS_ROUTER* instance);
extern int MaxScaleUptime();
extern void avro_get_used_tables(Avro *router, DCB *dcb);
bool converter_func(Worker::Call::action_t action, Avro* router);
bool binlog_next_file_exists(const char* binlogdir, const char* binlog);
int blr_file_get_next_binlogname(const char *router);
bool avro_load_conversion_state(Avro *router);
void avro_load_metadata_from_schemas(Avro *router);
int avro_client_callback(DCB *dcb, DCB_REASON reason, void *userdata);
static bool ensure_dir_ok(std::string path, int mode);
bool avro_save_conversion_state(Avro *router);
static void stats_func(void *);
void avro_index_file(Avro *router, const char* path);
void avro_update_index(Avro* router);
static bool conversion_task_ctl(Avro *inst, bool start);

static SPINLOCK instlock;
static Avro *instances;

bool avro_handle_convert(const MODULECMD_ARG *args, json_t** output)
{
    bool rval = false;

    if (strcmp(args->argv[1].value.string, "start") == 0 &&
        conversion_task_ctl((Avro*)args->argv[0].value.service->router_instance, true))
    {
        MXS_NOTICE("Started conversion for service '%s'.", args->argv[0].value.service->name);
        rval = true;
    }
    else if (strcmp(args->argv[1].value.string, "stop") == 0 &&
             conversion_task_ctl((Avro*)args->argv[0].value.service->router_instance, false))
    {
        MXS_NOTICE("Stopped conversion for service '%s'.", args->argv[0].value.service->name);
        rval = true;
    }

    return rval;
}


static const MXS_ENUM_VALUE codec_values[] =
{
    {"null", MXS_AVRO_CODEC_NULL},
    {"deflate",  MXS_AVRO_CODEC_DEFLATE},
// Not yet implemented
//    {"snappy", MXS_AVRO_CODEC_SNAPPY},
    {NULL}
};

static bool do_unlink(const char* format, ...)
{
    va_list args;
    va_start(args, format);

    char filename[PATH_MAX + 1];
    vsnprintf(filename, sizeof(filename), format, args);

    va_end(args);

    int rc = unlink(filename);
    return rc == 0 || rc == ENOENT;
}

static bool do_unlink_with_pattern(const char* format, ...)
{
    bool rval = true;
    va_list args;
    va_start(args, format);

    char filename[PATH_MAX + 1];
    vsnprintf(filename, sizeof(filename), format, args);

    va_end(args);

    glob_t g;
    int rc = glob(filename, 0, NULL, &g);

    if (rc == 0)
    {
        for (size_t i = 0; i < g.gl_pathc; i++)
        {
            if (!do_unlink("%s", g.gl_pathv[i]))
            {
                rval = false;
            }
        }
    }
    else if (rc != GLOB_NOMATCH)
    {
        modulecmd_set_error("Failed to search '%s': %d, %s",
                            filename, errno, mxs_strerror(errno));
        rval = false;
    }

    globfree(&g);

    return rval;
}

static bool avro_handle_purge(const MODULECMD_ARG *args, json_t** output)
{
    Avro* inst = (Avro*)args->argv[0].value.service->router_instance;

    // First stop the conversion service
    conversion_task_ctl(inst, false);

    // Then delete the files
    return do_unlink("%s/%s", inst->avrodir.c_str(), AVRO_PROGRESS_FILE) && // State file
           do_unlink("/%s/%s", inst->avrodir.c_str(), avro_index_name) &&   // Index database
           do_unlink_with_pattern("/%s/*.avro", inst->avrodir.c_str()) &&   // .avro files
           do_unlink_with_pattern("/%s/*.avsc", inst->avrodir.c_str());     // .avsc files
}

/**
 * The module entry point routine. It is this routine that
 * must populate the structure that is referred to as the
 * "module object", this is a structure with the set of
 * external entry points for this module.
 *
 * @return The module object
 */
extern "C" MXS_MODULE* MXS_CREATE_MODULE()
{
    spinlock_init(&instlock);
    instances = NULL;

    static modulecmd_arg_type_t args_convert[] =
    {
        { MODULECMD_ARG_SERVICE | MODULECMD_ARG_NAME_MATCHES_DOMAIN, "The avrorouter service" },
        { MODULECMD_ARG_STRING, "Action, whether to 'start' or 'stop' the conversion process" }
    };
    modulecmd_register_command(MXS_MODULE_NAME, "convert", MODULECMD_TYPE_ACTIVE,
                               avro_handle_convert, 2, args_convert,
                               "Start or stop the binlog to avro conversion process");

    static modulecmd_arg_type_t args_purge[] =
    {
        {
            MODULECMD_ARG_SERVICE | MODULECMD_ARG_NAME_MATCHES_DOMAIN,
            "The avrorouter service to purge (NOTE: THIS REMOVES ALL CONVERTED FILES)"
        }
    };
    modulecmd_register_command(MXS_MODULE_NAME, "purge",  MODULECMD_TYPE_ACTIVE,
                               avro_handle_purge, 1, args_purge,
                               "Purge created Avro files and reset conversion state. "
                               "NOTE: MaxScale must be restarted after this call.");

    static MXS_ROUTER_OBJECT MyObject =
    {
        createInstance,
        newSession,
        closeSession,
        freeSession,
        routeQuery,
        diagnostics,
        diagnostics_json,
        clientReply,
        errorReply,
        getCapabilities,
        NULL
    };

    static MXS_MODULE info =
    {
        MXS_MODULE_API_ROUTER,
        MXS_MODULE_GA,
        MXS_ROUTER_VERSION,
        "Binlogrouter",
        "V1.0.0",
        RCAP_TYPE_NO_RSESSION | RCAP_TYPE_NO_AUTH,
        &MyObject,
        NULL, /* Process init. */
        NULL, /* Process finish. */
        NULL, /* Thread init. */
        NULL, /* Thread finish. */
        {
            {
                "binlogdir",
                MXS_MODULE_PARAM_PATH,
                NULL,
                MXS_MODULE_OPT_PATH_R_OK |
                MXS_MODULE_OPT_PATH_W_OK |
                MXS_MODULE_OPT_PATH_X_OK |
                MXS_MODULE_OPT_PATH_CREAT
            },
            {
                "avrodir",
                MXS_MODULE_PARAM_PATH,
                MXS_DEFAULT_DATADIR,
                MXS_MODULE_OPT_PATH_R_OK |
                MXS_MODULE_OPT_PATH_W_OK |
                MXS_MODULE_OPT_PATH_X_OK |
                MXS_MODULE_OPT_PATH_CREAT
            },
            {"source", MXS_MODULE_PARAM_SERVICE},
            {"filestem", MXS_MODULE_PARAM_STRING, BINLOG_NAME_ROOT},
            {"group_rows", MXS_MODULE_PARAM_COUNT, "1000"},
            {"group_trx", MXS_MODULE_PARAM_COUNT, "1"},
            {"start_index", MXS_MODULE_PARAM_COUNT, "1"},
            {"block_size", MXS_MODULE_PARAM_SIZE, "0"},
            {"codec", MXS_MODULE_PARAM_ENUM, "null", MXS_MODULE_OPT_ENUM_UNIQUE, codec_values},
            {MXS_END_MODULE_PARAMS}
        }
    };

    return &info;
}

/**
 * Create the required tables in the sqlite database
 *
 * @param handle SQLite handle
 * @return True on success, false on error
 */
bool create_tables(sqlite3* handle)
{
    char* errmsg;
    int rc = sqlite3_exec(handle, "CREATE TABLE IF NOT EXISTS "
                          GTID_TABLE_NAME"(domain int, server_id int, "
                          "sequence bigint, "
                          "avrofile varchar(255), "
                          "position bigint, "
                          "primary key(domain, server_id, sequence, avrofile));",
                          NULL, NULL, &errmsg);
    if (rc != SQLITE_OK)
    {
        MXS_ERROR("Failed to create GTID index table '" GTID_TABLE_NAME "': %s",
                  sqlite3_errmsg(handle));
        sqlite3_free(errmsg);
        return false;
    }

    rc = sqlite3_exec(handle, "CREATE TABLE IF NOT EXISTS "
                      USED_TABLES_TABLE_NAME"(domain int, server_id int, "
                      "sequence bigint, binlog_timestamp bigint, "
                      "table_name varchar(255));",
                      NULL, NULL, &errmsg);
    if (rc != SQLITE_OK)
    {
        MXS_ERROR("Failed to create used tables table '" USED_TABLES_TABLE_NAME "': %s",
                  sqlite3_errmsg(handle));
        sqlite3_free(errmsg);
        return false;
    }

    rc = sqlite3_exec(handle, "CREATE TABLE IF NOT EXISTS "
                      INDEX_TABLE_NAME"(position bigint, filename varchar(255));",
                      NULL, NULL, &errmsg);
    if (rc != SQLITE_OK)
    {
        MXS_ERROR("Failed to create indexing progress table '" INDEX_TABLE_NAME "': %s",
                  sqlite3_errmsg(handle));
        sqlite3_free(errmsg);
        return false;
    }

    rc = sqlite3_exec(handle, "ATTACH DATABASE ':memory:' AS " MEMORY_DATABASE_NAME,
                      NULL, NULL, &errmsg);
    if (rc != SQLITE_OK)
    {
        MXS_ERROR("Failed to attach in-memory database '" MEMORY_DATABASE_NAME "': %s",
                  sqlite3_errmsg(handle));
        sqlite3_free(errmsg);
        return false;
    }

    rc = sqlite3_exec(handle, "CREATE TABLE " MEMORY_TABLE_NAME
                      "(domain int, server_id int, "
                      "sequence bigint, binlog_timestamp bigint, "
                      "table_name varchar(255), primary key (domain, server_id, sequence, table_name));",
                      NULL, NULL, &errmsg);
    if (rc != SQLITE_OK)
    {
        MXS_ERROR("Failed to create in-memory used tables table '" MEMORY_DATABASE_NAME
                  "." MEMORY_TABLE_NAME "': %s",
                  sqlite3_errmsg(handle));
        sqlite3_free(errmsg);
        return false;
    }

    return true;
}

static bool conversion_task_ctl(Avro *inst, bool start)
{
    bool rval = false;

    if (!inst->service->svc_do_shutdown)
    {
        Worker* worker = static_cast<Worker*>(mxs_rworker_get(MXS_RWORKER_MAIN));

        if (inst->task_handle)
        {
            worker->cancel_delayed_call(inst->task_handle);
            inst->task_handle = 0;
        }

        if (start)
        {
            inst->task_handle = worker->delayed_call(1000, converter_func, inst);
        }

        rval = true;
    }

    return rval;
}

/**
 * @brief Read router options from an external binlogrouter service
 *
 * This reads common options used by both the avrorouter and the binlogrouter
 * from a service that uses the binlogrouter. This way the basic configuration
 * details can be read from another service without the need to configure the
 * avrorouter with identical router options.
 *
 * @param inst Avro router instance
 * @param options The @c router_options of a binlogrouter instance
 */
void Avro::read_source_service_options(SERVICE* source)
{
    char** options = source->routerOptions;
    MXS_CONFIG_PARAMETER* params = source->svc_config_param;

    for (MXS_CONFIG_PARAMETER* p = params; p; p = p->next)
    {
        if (strcmp(p->name, "binlogdir") == 0)
        {
            binlogdir = p->value;
        }
        else if (strcmp(p->name, "filestem") == 0)
        {
            filestem = p->value;
        }
    }

    if (options)
    {
        for (int i = 0; options[i]; i++)
        {
            char option[strlen(options[i]) + 1];
            strcpy(option, options[i]);

            char *value = strchr(option, '=');
            if (value)
            {
                *value++ = '\0';
                value = trim(value);

                if (strcmp(option, "binlogdir") == 0)
                {
                    binlogdir = value;
                }
                else if (strcmp(option, "filestem") == 0)
                {
                    filestem = value;
                }
            }
        }
    }
}

/**
 * Create an instance of the router for a particular service
 * within MaxScale.
 *
 * The process of creating the instance causes the router to register
 * with the master server and begin replication of the binlogs from
 * the master server to MaxScale.
 *
 * @param service   The service this router is being create for
 * @param options   An array of options for this query router
 *
 * @return The instance data for this new instance
 */
MXS_ROUTER* createInstance(SERVICE *service, char **options)
{
    return Avro::create(service);
}

//static
Avro* Avro::create(SERVICE* service)
{
    bool err = false;
    SERVICE* source_service = NULL;
    MXS_CONFIG_PARAMETER *param = config_get_param(service->svc_config_param, "source");

    if (param)
    {
        SERVICE *source = service_find(param->value);
        ss_dassert(source);

        if (source)
        {
            if (strcmp(source->routerModule, "binlogrouter") == 0)
            {
                MXS_INFO("Using configuration options from service '%s'.", source->name);
                source_service = source;
            }
            else
            {
                MXS_ERROR("Service '%s' uses router module '%s' instead of "
                          "'binlogrouter'.", source->name, source->routerModule);
                err = true;
            }
        }
        else
        {
            MXS_ERROR("Service '%s' not found.", param->value);
            err = true;
        }
    }

    const int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE;
    sqlite3* sqlite_handle;
    const char* avrodir = config_get_string(service->svc_config_param, "avrodir");
    char dbpath[PATH_MAX + 1];
    snprintf(dbpath, sizeof(dbpath), "/%s/%s", avrodir, avro_index_name);

    if (sqlite3_open_v2(dbpath, &sqlite_handle, flags, NULL) != SQLITE_OK)
    {
        MXS_ERROR("Failed to open SQLite database '%s': %s", dbpath, sqlite3_errmsg(sqlite_handle));
        err = true;
    }
    else if (!create_tables(sqlite_handle))
    {
        err = true;
    }

    if (err)
    {
        sqlite3_close_v2(sqlite_handle);
        return NULL;
    }

    return new (std::nothrow) Avro(service, service->svc_config_param,
                                   sqlite_handle, source_service);
}

Avro::Avro(SERVICE* service, MXS_CONFIG_PARAMETER* params, sqlite3* handle, SERVICE* source):
    service(service),
    filestem(config_get_string(params, "filestem")),
    binlogdir(config_get_string(params, "binlogdir")),
    avrodir(config_get_string(params, "avrodir")),
    current_pos(4),
    binlog_fd(-1),
    trx_count(0),
    trx_target(config_get_integer(params, "group_trx")),
    row_count(0),
    row_target(config_get_integer(params, "group_rows")),
    block_size(config_get_size(params, "block_size")),
    codec(static_cast<mxs_avro_codec_type>(config_get_enum(params, "codec", codec_values))),
    sqlite_handle(handle),
    task_handle(0),
    stats{0}
{
    int pcreerr;
    size_t erroff;
    create_table_re = pcre2_compile((PCRE2_SPTR) create_table_regex, PCRE2_ZERO_TERMINATED,
                                    0, &pcreerr, &erroff, NULL);
    ss_dassert(create_table_re); // This should never fail
    alter_table_re = pcre2_compile((PCRE2_SPTR) alter_table_regex, PCRE2_ZERO_TERMINATED,
                                   0, &pcreerr, &erroff, NULL);
    ss_dassert(alter_table_re); // This should never fail

    if (source)
    {
        read_source_service_options(source);
    }

    char filename[BINLOG_FNAMELEN + 1];
    snprintf(filename, sizeof(filename), BINLOG_NAMEFMT, filestem.c_str(),
             config_get_integer(params, "start_index"));
    binlog_name = filename;

    MXS_NOTICE("Reading MySQL binlog files from %s", binlogdir.c_str());
    MXS_NOTICE("Avro files stored at: %s", avrodir.c_str());
    MXS_NOTICE("First binlog is: %s", binlog_name.c_str());

    // TODO: Do these in Avro::create
    avro_load_conversion_state(this);
    avro_load_metadata_from_schemas(this);

    /* Start the scan, read, convert AVRO task */
    conversion_task_ctl(this, true);


}

/**
 * Associate a new session with this instance of the router.
 *
 * In the case of the avrorouter a new session equates to a new slave
 * connecting to MaxScale and requesting binlog records. We need to go
 * through the slave registration process for this new slave.
 *
 * @param instance  The router instance data
 * @param session   The session itself
 * @return Session specific data for this session
 */
static MXS_ROUTER_SESSION *
newSession(MXS_ROUTER *instance, MXS_SESSION *session)
{
    Avro* inst = reinterpret_cast<Avro*>(instance);
    return AvroSession::create(inst, session);
}

/**
 * The session is no longer required. Shutdown all operation and free memory
 * associated with this session. In this case a single session is associated
 * to a slave of MaxScale. Therefore this is called when that slave is no
 * longer active and should remove of reference to that slave, free memory
 * and prevent any further forwarding of binlog records to that slave.
 *
 * Parameters:
 * @param router_instance   The instance of the router
 * @param router_cli_ses    The particular session to free
 *
 */
static void freeSession(MXS_ROUTER* router_instance, MXS_ROUTER_SESSION* router_client_ses)
{
    AvroSession *client = (AvroSession *) router_client_ses;
    delete client;
}

/**
 * Close a session with the router, this is the mechanism
 * by which a router may cleanup data structure etc.
 *
 * @param instance          The router instance data
 * @param router_session    The session being closed
 */
static void closeSession(MXS_ROUTER *instance, MXS_ROUTER_SESSION *router_session)
{
}

/**
 * We have data from the client, this is likely to be packets related to
 * the registration of the slave to receive binlog records. Unlike most
 * MaxScale routers there is no forwarding to the backend database, merely
 * the return of either predefined server responses that have been cached
 * or binlog records.
 *
 * @param instance      The router instance
 * @param router_session    The router session returned from the newSession call
 * @param queue         The queue of data buffers to route
 * @return 1 on success, 0 on error
 */
static int
routeQuery(MXS_ROUTER *instance, MXS_ROUTER_SESSION *router_session, GWBUF *queue)
{
    Avro *router = (Avro *) instance;
    AvroSession *client = (AvroSession *) router_session;

    return avro_client_handle_request(router, client, queue);
}

/**
 * Display router diagnostics
 *
 * @param instance  Instance of the router
 * @param dcb       DCB to send diagnostics to
 */
static void
diagnostics(MXS_ROUTER *router, DCB *dcb)
{
    Avro *router_inst = (Avro *) router;

    dcb_printf(dcb, "\tAVRO files directory:                %s\n",
               router_inst->avrodir.c_str());

    dcb_printf(dcb, "\tBinlog directory:                    %s\n",
               router_inst->binlogdir.c_str());
    dcb_printf(dcb, "\tCurrent binlog file:                 %s\n",
               router_inst->binlog_name.c_str());
    dcb_printf(dcb, "\tCurrent binlog position:             %lu\n",
               router_inst->current_pos);
    dcb_printf(dcb, "\tCurrent GTID value:                  %lu-%lu-%lu\n",
               router_inst->gtid.domain, router_inst->gtid.server_id,
               router_inst->gtid.seq);
    dcb_printf(dcb, "\tCurrent GTID timestamp:              %u\n",
               router_inst->gtid.timestamp);
    dcb_printf(dcb, "\tCurrent GTID #events:                %lu\n",
               router_inst->gtid.event_num);

    dcb_printf(dcb, "\tCurrent GTID affected tables: ");
    avro_get_used_tables(router_inst, dcb);
    dcb_printf(dcb, "\n");

    dcb_printf(dcb, "\tNumber of AVRO clients:              %u\n",
               router_inst->stats.n_clients);

}

/**
 * Display router diagnostics
 *
 * @param instance  Instance of the router
 */
static json_t* diagnostics_json(const MXS_ROUTER *router)
{
    Avro *router_inst = (Avro *)router;

    json_t* rval = json_object();

    char pathbuf[PATH_MAX + 1];
    snprintf(pathbuf, sizeof(pathbuf), "%s/%s", router_inst->avrodir.c_str(), AVRO_PROGRESS_FILE);

    json_object_set_new(rval, "infofile", json_string(pathbuf));
    json_object_set_new(rval, "avrodir", json_string(router_inst->avrodir.c_str()));
    json_object_set_new(rval, "binlogdir", json_string(router_inst->binlogdir.c_str()));
    json_object_set_new(rval, "binlog_name", json_string(router_inst->binlog_name.c_str()));
    json_object_set_new(rval, "binlog_pos", json_integer(router_inst->current_pos));

    snprintf(pathbuf, sizeof(pathbuf), "%lu-%lu-%lu", router_inst->gtid.domain,
             router_inst->gtid.server_id, router_inst->gtid.seq);
    json_object_set_new(rval, "gtid", json_string(pathbuf));
    json_object_set_new(rval, "gtid_timestamp", json_integer(router_inst->gtid.timestamp));
    json_object_set_new(rval, "gtid_event_number", json_integer(router_inst->gtid.event_num));
    json_object_set_new(rval, "clients", json_integer(router_inst->stats.n_clients));

    return rval;
}

/**
 * Client Reply routine - in this case this is a message from the
 * master server, It should be sent to the state machine that manages
 * master packets as it may be binlog records or part of the registration
 * handshake that takes part during connection establishment.
 *
 *
 * @param       instance        The router instance
 * @param       router_session  The router session
 * @param       master_dcb      The DCB for the connection to the master
 * @param       queue           The GWBUF with reply data
 */
static void
clientReply(MXS_ROUTER *instance, MXS_ROUTER_SESSION *router_session, GWBUF *queue, DCB *backend_dcb)
{
    /** We should never end up here */
    ss_dassert(false);
}

/**
 * Error Reply routine
 *
 * The routine will reply to client errors and/or closing the session
 * or try to open a new backend connection.
 *
 * @param       instance        The router instance
 * @param       router_session  The router session
 * @param       message         The error message to reply
 * @param       backend_dcb     The backend DCB
 * @param       action      The action: ERRACT_NEW_CONNECTION or ERRACT_REPLY_CLIENT
 * @param   succp       Result of action: true iff router can continue
 *
 */
static void
errorReply(MXS_ROUTER *instance, MXS_ROUTER_SESSION *router_session, GWBUF *message, DCB *backend_dcb,
           mxs_error_action_t action,
           bool *succp)
{
    /** We should never end up here */
    ss_dassert(false);
}

static uint64_t getCapabilities(MXS_ROUTER* instance)
{
    return RCAP_TYPE_NONE;
}

/**
 * Conversion task: MySQL binlogs to AVRO files
 */
bool converter_func(Worker::Call::action_t action, Avro* router)
{
    if (action == Worker::Call::CANCEL)
    {
        return false;
    }

    bool progress = false;
    avro_binlog_end_t binlog_end = AVRO_BINLOG_ERROR;

    uint64_t start_pos = router->current_pos;
    std::string binlog_name = router->binlog_name;

    if (avro_open_binlog(router->binlogdir.c_str(), router->binlog_name.c_str(), &router->binlog_fd))
    {
        binlog_end = avro_read_all_events(router);

        if (router->current_pos != start_pos || binlog_name != router->binlog_name)
        {
            /** Update the GTID index */
            avro_update_index(router);
            progress = true;
        }

        avro_close_binlog(router->binlog_fd);
    }

    static int logged = true;

    /** We reached end of file, flush unwritten records to disk */
    if (progress)
    {
        avro_flush_all_tables(router, AVROROUTER_FLUSH);
        avro_save_conversion_state(router);
        logged = false;
    }

    if (binlog_end == AVRO_LAST_FILE && !logged)
    {
        logged = true;
        MXS_INFO("Stopped processing file %s at position %lu. Waiting until"
                 " more data is written before continuing.",
                 router->binlog_name.c_str(), router->current_pos);
    }

    return true;
}
