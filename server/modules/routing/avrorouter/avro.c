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

#include "avrorouter.h"

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
#include <maxscale/housekeeper.h>
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
#include <binlog_common.h>

#ifndef BINLOG_NAMEFMT
#define BINLOG_NAMEFMT      "%s.%06d"
#endif

#define AVRO_TASK_DELAY_MAX 15

static const char* avro_task_name = "binlog_to_avro";
static const char* index_task_name = "avro_indexing";
static const char* avro_index_name = "avro.index";

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
extern void avro_get_used_tables(AVRO_INSTANCE *router, DCB *dcb);
void converter_func(void* data);
bool binlog_next_file_exists(const char* binlogdir, const char* binlog);
int blr_file_get_next_binlogname(const char *router);
bool avro_load_conversion_state(AVRO_INSTANCE *router);
void avro_load_metadata_from_schemas(AVRO_INSTANCE *router);
int avro_client_callback(DCB *dcb, DCB_REASON reason, void *userdata);
static bool ensure_dir_ok(const char* path, int mode);
bool avro_save_conversion_state(AVRO_INSTANCE *router);
static void stats_func(void *);
void avro_index_file(AVRO_INSTANCE *router, const char* path);
void avro_update_index(AVRO_INSTANCE* router);
static bool conversion_task_ctl(AVRO_INSTANCE *inst, bool start);

static SPINLOCK instlock;
static AVRO_INSTANCE *instances;

bool avro_handle_convert(const MODULECMD_ARG *args, json_t** output)
{
    bool rval = false;

    if (strcmp(args->argv[1].value.string, "start") == 0 &&
        conversion_task_ctl((AVRO_INSTANCE*)args->argv[0].value.service->router_instance, true))
    {
        MXS_NOTICE("Started conversion for service '%s'.", args->argv[0].value.service->name);
        rval = true;
    }
    else if (strcmp(args->argv[1].value.string, "stop") == 0 &&
             conversion_task_ctl((AVRO_INSTANCE*)args->argv[0].value.service->router_instance, false))
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
    AVRO_INSTANCE* inst = (AVRO_INSTANCE*)args->argv[0].value.service->router_instance;

    // First stop the conversion service
    conversion_task_ctl(inst, false);

    // Then delete the files
    return do_unlink("%s/%s", inst->avrodir, AVRO_PROGRESS_FILE) && // State file
           do_unlink("/%s/%s", inst->avrodir, avro_index_name) &&   // Index database
           do_unlink_with_pattern("/%s/*.avro", inst->avrodir) &&   // .avro files
           do_unlink_with_pattern("/%s/*.avsc", inst->avrodir);     // .avsc files
}

/**
 * The module entry point routine. It is this routine that
 * must populate the structure that is referred to as the
 * "module object", this is a structure with the set of
 * external entry points for this module.
 *
 * @return The module object
 */
MXS_MODULE* MXS_CREATE_MODULE()
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
                MXS_MODULE_OPT_PATH_CREAT
            },
            {
                "avrodir",
                MXS_MODULE_PARAM_PATH,
                MXS_DEFAULT_DATADIR,
                MXS_MODULE_OPT_PATH_R_OK |
                MXS_MODULE_OPT_PATH_W_OK |
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
        MXS_ERROR("Failed to create GTID index table '"GTID_TABLE_NAME"': %s",
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
        MXS_ERROR("Failed to create used tables table '"USED_TABLES_TABLE_NAME"': %s",
                  sqlite3_errmsg(handle));
        sqlite3_free(errmsg);
        return false;
    }

    rc = sqlite3_exec(handle, "CREATE TABLE IF NOT EXISTS "
                      INDEX_TABLE_NAME"(position bigint, filename varchar(255));",
                      NULL, NULL, &errmsg);
    if (rc != SQLITE_OK)
    {
        MXS_ERROR("Failed to create indexing progress table '"INDEX_TABLE_NAME"': %s",
                  sqlite3_errmsg(handle));
        sqlite3_free(errmsg);
        return false;
    }

    rc = sqlite3_exec(handle, "ATTACH DATABASE ':memory:' AS "MEMORY_DATABASE_NAME,
                      NULL, NULL, &errmsg);
    if (rc != SQLITE_OK)
    {
        MXS_ERROR("Failed to attach in-memory database '"MEMORY_DATABASE_NAME"': %s",
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
        MXS_ERROR("Failed to create in-memory used tables table '"MEMORY_DATABASE_NAME
                  "."MEMORY_TABLE_NAME"': %s",
                  sqlite3_errmsg(handle));
        sqlite3_free(errmsg);
        return false;
    }

    return true;
}

static bool conversion_task_ctl(AVRO_INSTANCE *inst, bool start)
{
    bool rval = false;

    if (!inst->service->svc_do_shutdown)
    {
        char tasknm[strlen(avro_task_name) + strlen(inst->service->name) + 2];
        snprintf(tasknm, sizeof(tasknm), "%s-%s", inst->service->name, avro_task_name);

        /** Remove old task and create a new one */
        hktask_remove(tasknm);

        if (!start || hktask_add(tasknm, converter_func, inst, inst->task_delay))
        {
            rval = true;
        }
        else
        {
            MXS_ERROR("Failed to add binlog to Avro conversion task to housekeeper.");
        }
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
void read_source_service_options(AVRO_INSTANCE *inst, const char** options,
                                 MXS_CONFIG_PARAMETER* params)
{
    for (MXS_CONFIG_PARAMETER* p = params; p; p = p->next)
    {
        if (strcmp(p->name, "binlogdir") == 0)
        {
            MXS_FREE(inst->binlogdir);
            inst->binlogdir = MXS_STRDUP_A(p->value);
        }
        else if (strcmp(p->name, "filestem") == 0)
        {
            MXS_FREE(inst->fileroot);
            inst->fileroot = MXS_STRDUP_A(p->value);
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
                    MXS_FREE(inst->binlogdir);
                    inst->binlogdir = MXS_STRDUP_A(value);
                }
                else if (strcmp(option, "filestem") == 0)
                {
                    MXS_FREE(inst->fileroot);
                    inst->fileroot = MXS_STRDUP_A(value);
                }
            }
        }
    }
}

/**
 * TABLE_CREATE free function for use with hashtable.
 * @param v Pointer to a TABLE_CREATE
 */
static void table_create_hfree(void* v)
{
    table_create_free((TABLE_CREATE*)v);
}

/**
 * AVRO_TABLE free function for use with hashtable.
 * @param v Pointer to a AVRO_TABLE
 */
static void avro_table_hfree(void* v)
{
    avro_table_free((AVRO_TABLE*)v);
}

/**
 * TABLE_MAP free function for use with hashtable.
 * @param v Pointer to a TABLE_MAP
 */
static void table_map_hfree(void* v)
{
    table_map_free((TABLE_MAP*)v);
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
static MXS_ROUTER *
createInstance(SERVICE *service, char **options)
{
    AVRO_INSTANCE *inst;
    int i;

    if ((inst = MXS_CALLOC(1, sizeof(AVRO_INSTANCE))) == NULL)
    {
        return NULL;
    }

    memset(&inst->stats, 0, sizeof(AVRO_ROUTER_STATS));
    spinlock_init(&inst->lock);
    spinlock_init(&inst->fileslock);
    inst->service = service;
    inst->binlog_fd = -1;
    inst->current_pos = 4;
    inst->binlog_position = 4;
    inst->clients = NULL;
    inst->next = NULL;
    inst->lastEventTimestamp = 0;
    inst->binlog_position = 0;
    inst->task_delay = 1;
    inst->row_count = 0;
    inst->trx_count = 0;
    inst->binlogdir = NULL;

    MXS_CONFIG_PARAMETER *params = service->svc_config_param;

    inst->avrodir = MXS_STRDUP_A(config_get_string(params, "avrodir"));
    inst->fileroot = MXS_STRDUP_A(config_get_string(params, "filestem"));
    inst->row_target = config_get_integer(params, "group_rows");
    inst->trx_target = config_get_integer(params, "group_trx");
    inst->codec = config_get_enum(params, "codec", codec_values);
    int first_file = config_get_integer(params, "start_index");
    inst->block_size = config_get_size(params, "block_size");

    MXS_CONFIG_PARAMETER *param = config_get_param(params, "source");
    inst->gtid.domain = 0;
    inst->gtid.event_num = 0;
    inst->gtid.seq = 0;
    inst->gtid.server_id = 0;
    inst->gtid.timestamp = 0;
    memset(&inst->active_maps, 0, sizeof(inst->active_maps));
    bool err = false;

    if (param)
    {
        SERVICE *source = service_find(param->value);
        ss_dassert(source);

        if (source)
        {
            if (strcmp(source->routerModule, "binlogrouter") == 0)
            {
                MXS_NOTICE("[%s] Using configuration options from service '%s'.",
                           service->name, source->name);
                read_source_service_options(inst, (const char**)source->routerOptions,
                                            source->svc_config_param);
            }
            else
            {
                MXS_ERROR("[%s] Service '%s' uses router module '%s' instead of"
                          " 'binlogrouter'.", service->name, source->name,
                          source->routerModule);
                err = true;
            }
        }
    }

    param = config_get_param(params, "binlogdir");

    if (param)
    {
        MXS_FREE(inst->binlogdir);
        inst->binlogdir = MXS_STRDUP_A(param->value);
    }

    if (options)
    {
        MXS_WARNING("Router options for Avrorouter are deprecated. Please convert them to parameters.");

        for (i = 0; options[i]; i++)
        {
            char *value;
            if ((value = strchr(options[i], '=')))
            {
                *value++ = '\0';
                trim(value);
                trim(options[i]);

                if (strcmp(options[i], "binlogdir") == 0)
                {
                    MXS_FREE(inst->binlogdir);
                    inst->binlogdir = MXS_STRDUP_A(value);
                }
                else if (strcmp(options[i], "avrodir") == 0)
                {
                    MXS_FREE(inst->avrodir);
                    inst->avrodir = MXS_STRDUP_A(value);
                }
                else if (strcmp(options[i], "filestem") == 0)
                {
                    MXS_FREE(inst->fileroot);
                    inst->fileroot = MXS_STRDUP_A(value);
                }
                else if (strcmp(options[i], "group_rows") == 0)
                {
                    inst->row_target = atoi(value);
                }
                else if (strcmp(options[i], "group_trx") == 0)
                {
                    inst->trx_target = atoi(value);
                }
                else if (strcmp(options[i], "start_index") == 0)
                {
                    first_file = MXS_MAX(1, atoi(value));
                }
                else if (strcmp(options[i], "block_size") == 0)
                {
                    inst->block_size = atoi(value);
                }
                else
                {
                    MXS_WARNING("Unknown router option: '%s'", options[i]);
                    err = true;
                }
            }
            else
            {
                MXS_WARNING("Unknown router option: '%s'", options[i]);
                err = true;
            }
        }
    }

    if (inst->binlogdir == NULL)
    {
        MXS_ERROR("No 'binlogdir' option found in source service, in parameters or in router_options.");
        err = true;
    }
    else if (inst->fileroot == NULL)
    {
        MXS_ERROR("No 'filestem' option found in source service, in parameters or in router_options.");
        err = true;
    }
    else if (ensure_dir_ok(inst->binlogdir, R_OK) && ensure_dir_ok(inst->avrodir, W_OK))
    {
        snprintf(inst->binlog_name, sizeof(inst->binlog_name), BINLOG_NAMEFMT, inst->fileroot, first_file);
        inst->prevbinlog[0] = '\0';

        MXS_NOTICE("[%s] Reading MySQL binlog files from %s", service->name, inst->binlogdir);
        MXS_NOTICE("[%s] Avro files stored at: %s", service->name, inst->avrodir);
        MXS_NOTICE("[%s] First binlog is: %s", service->name, inst->binlog_name);
    }

    if ((inst->table_maps = hashtable_alloc(1000, hashtable_item_strhash, hashtable_item_strcmp)) &&
        (inst->open_tables = hashtable_alloc(1000, hashtable_item_strhash, hashtable_item_strcmp)) &&
        (inst->created_tables = hashtable_alloc(1000, hashtable_item_strhash, hashtable_item_strcmp)))
    {
        hashtable_memory_fns(inst->table_maps, hashtable_item_strdup, NULL,
                             hashtable_item_free, table_map_hfree);
        hashtable_memory_fns(inst->open_tables, hashtable_item_strdup, NULL,
                             hashtable_item_free, avro_table_hfree);
        hashtable_memory_fns(inst->created_tables, hashtable_item_strdup, NULL,
                             hashtable_item_free, table_create_hfree);
    }
    else
    {
        MXS_ERROR("Hashtable allocation failed. This is most likely caused "
                  "by a lack of available memory.");
        err = true;
    }

    int pcreerr;
    size_t erroff;
    pcre2_code *create_re = pcre2_compile((PCRE2_SPTR) create_table_regex,
                                          PCRE2_ZERO_TERMINATED, 0, &pcreerr, &erroff, NULL);
    ss_dassert(create_re); // This should almost never fail
    pcre2_code *alter_re = pcre2_compile((PCRE2_SPTR) alter_table_regex,
                                         PCRE2_ZERO_TERMINATED, 0, &pcreerr, &erroff, NULL);
    ss_dassert(alter_re); // This should almost never fail

    if (create_re && alter_re)
    {
        inst->create_table_re = create_re;
        inst->alter_table_re = alter_re;
    }
    else
    {
        err = true;
    }

    char dbpath[PATH_MAX + 1];
    snprintf(dbpath, sizeof(dbpath), "/%s/%s", inst->avrodir, avro_index_name);

    if (access(dbpath, W_OK) == 0)
    {
        MXS_NOTICE("[%s] Using existing GTID index: %s", service->name, dbpath);
    }

    if (sqlite3_open_v2(dbpath, &inst->sqlite_handle,
                        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL) != SQLITE_OK)
    {
        MXS_ERROR("Failed to open SQLite database '%s': %s", dbpath,
                  sqlite3_errmsg(inst->sqlite_handle));
        err = true;
    }
    else if (!create_tables(inst->sqlite_handle))
    {
        err = true;
    }

    if (err)
    {
        sqlite3_close_v2(inst->sqlite_handle);
        hashtable_free(inst->table_maps);
        hashtable_free(inst->open_tables);
        hashtable_free(inst->created_tables);
        MXS_FREE(inst->avrodir);
        MXS_FREE(inst->binlogdir);
        MXS_FREE(inst->fileroot);
        MXS_FREE(inst);
        return NULL;
    }
    /**
     * We have completed the creation of the instance data, so now
     * insert this router instance into the linked list of routers
     * that have been created with this module.
     */
    spinlock_acquire(&instlock);
    inst->next = instances;
    instances = inst;
    spinlock_release(&instlock);

    /* AVRO converter init */
    avro_load_conversion_state(inst);
    avro_load_metadata_from_schemas(inst);

    /*
     * Add tasks for statistic computation
     */
    /** Not used currenly
    snprintf(task_name, BLRM_TASK_NAME_LEN, "%s stats", service->name);
    hktask_add(task_name, stats_func, inst, AVRO_STATS_FREQ);
     */

    /* Start the scan, read, convert AVRO task */
    conversion_task_ctl(inst, true);

    MXS_INFO("current MySQL binlog file is %s, pos is %lu\n",
             inst->binlog_name, inst->current_pos);

    return (MXS_ROUTER *) inst;
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
    AVRO_INSTANCE *inst = (AVRO_INSTANCE *) instance;
    AVRO_CLIENT *client;

    MXS_DEBUG("%lu [newSession] new router session with "
              "session %p, and inst %p.", pthread_self(), session, inst);

    if ((client = (AVRO_CLIENT *) MXS_CALLOC(1, sizeof(AVRO_CLIENT))) == NULL)
    {
        return NULL;
    }

#if defined(SS_DEBUG)
    client->rses_chk_top = CHK_NUM_ROUTER_SES;
    client->rses_chk_tail = CHK_NUM_ROUTER_SES;
#endif

    memset(&client->stats, 0, sizeof(AVRO_CLIENT_STATS));
    atomic_add(&inst->stats.n_clients, 1);
    client->uuid = NULL;
    spinlock_init(&client->catch_lock);
    client->dcb = session->client_dcb;
    client->router = inst;
    client->format = AVRO_FORMAT_UNDEFINED;

    client->cstate = 0;

    client->connect_time = time(0);
    client->last_sent_pos = 0;
    memset(&client->gtid, 0, sizeof(client->gtid));
    memset(&client->gtid_start, 0, sizeof(client->gtid_start));

    /* Set initial state of the slave */
    client->state = AVRO_CLIENT_UNREGISTERED;
    char dbpath[PATH_MAX + 1];
    snprintf(dbpath, sizeof(dbpath), "/%s/%s", inst->avrodir, avro_index_name);

    /** A new handle for each client allows thread-safe use of the sqlite database */
    if (sqlite3_open_v2(dbpath, &client->sqlite_handle,
                        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL) != SQLITE_OK)
    {
        MXS_ERROR("Failed to open SQLite database '%s': %s", dbpath,
                  sqlite3_errmsg(inst->sqlite_handle));
        sqlite3_close_v2(client->sqlite_handle);
    }

    /**
     * Add this session to the list of active sessions.
     */
    spinlock_acquire(&inst->lock);
    client->next = inst->clients;
    inst->clients = client;
    spinlock_release(&inst->lock);

    CHK_CLIENT_RSES(client);

    return (void *) client;
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
    AVRO_INSTANCE *router = (AVRO_INSTANCE *) router_instance;
    AVRO_CLIENT *client = (AVRO_CLIENT *) router_client_ses;

    ss_debug(int prev_val = )atomic_add(&router->stats.n_clients, -1);
    ss_dassert(prev_val > 0);

    free(client->uuid);
    maxavro_file_close(client->file_handle);
    sqlite3_close_v2(client->sqlite_handle);

    /*
     * Remove the slave session form the list of slaves that are using the
     * router currently.
     */
    spinlock_acquire(&router->lock);
    if (router->clients == client)
    {
        router->clients = client->next;
    }
    else
    {
        AVRO_CLIENT *ptr = router->clients;

        while (ptr != NULL && ptr->next != client)
        {
            ptr = ptr->next;
        }

        if (ptr != NULL)
        {
            ptr->next = client->next;
        }
    }
    spinlock_release(&router->lock);

    MXS_FREE(client);
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
    AVRO_INSTANCE *router = (AVRO_INSTANCE *) instance;
    AVRO_CLIENT *client = (AVRO_CLIENT *) router_session;

    CHK_CLIENT_RSES(client);

    spinlock_acquire(&client->catch_lock);
    spinlock_acquire(&client->file_lock);

    client->state = AVRO_CLIENT_UNREGISTERED;

    spinlock_release(&client->file_lock);
    spinlock_release(&client->catch_lock);
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
    AVRO_INSTANCE *router = (AVRO_INSTANCE *) instance;
    AVRO_CLIENT *client = (AVRO_CLIENT *) router_session;

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
    AVRO_INSTANCE *router_inst = (AVRO_INSTANCE *) router;
    AVRO_CLIENT *session;
    int i = 0;
    char buf[40];
    struct tm tm;

    spinlock_acquire(&router_inst->lock);
    session = router_inst->clients;
    while (session)
    {
        i++;
        session = session->next;
    }
    spinlock_release(&router_inst->lock);

    dcb_printf(dcb, "\tAVRO Converter infofile:             %s/%s\n",
               router_inst->avrodir, AVRO_PROGRESS_FILE);
    dcb_printf(dcb, "\tAVRO files directory:                %s\n",
               router_inst->avrodir);

    localtime_r(&router_inst->stats.lastReply, &tm);
    asctime_r(&tm, buf);

    dcb_printf(dcb, "\tBinlog directory:                    %s\n",
               router_inst->binlogdir);
    dcb_printf(dcb, "\tCurrent binlog file:                 %s\n",
               router_inst->binlog_name);
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

    if (router_inst->clients)
    {
        dcb_printf(dcb, "\tClients:\n");
        spinlock_acquire(&router_inst->lock);
        session = router_inst->clients;
        while (session)
        {

            char sync_marker_hex[SYNC_MARKER_SIZE * 2 + 1];

            dcb_printf(dcb, "\t\tClient UUID:                 %s\n", session->uuid);
            dcb_printf(dcb, "\t\tClient_host_port:            [%s]:%d\n",
                       session->dcb->remote, dcb_get_port(session->dcb));
            dcb_printf(dcb, "\t\tUsername:                    %s\n", session->dcb->user);
            dcb_printf(dcb, "\t\tClient DCB:                  %p\n", session->dcb);
            dcb_printf(dcb, "\t\tClient protocol:             %s\n",
                       session->dcb->service->ports->protocol);
            dcb_printf(dcb, "\t\tClient Output Format:        %s\n",
                       avro_client_ouput[session->format]);
            dcb_printf(dcb, "\t\tState:                       %s\n",
                       avro_client_states[session->state]);
            dcb_printf(dcb, "\t\tAvro file:                   %s\n", session->avro_binfile);

            gw_bin2hex(sync_marker_hex, session->avro_file.sync, SYNC_MARKER_SIZE);

            dcb_printf(dcb, "\t\tAvro file SyncMarker:        %s\n", sync_marker_hex);
            dcb_printf(dcb, "\t\tAvro file last read block:   %lu\n",
                       session->avro_file.blocks_read);
            dcb_printf(dcb, "\t\tAvro file last read record:  %lu\n",
                       session->avro_file.records_read);

            if (session->gtid_start.domain > 0 || session->gtid_start.server_id > 0 ||
                session->gtid_start.seq > 0)
            {
                dcb_printf(dcb, "\t\tRequested GTID:          %lu-%lu-%lu\n",
                           session->gtid_start.domain, session->gtid_start.server_id,
                           session->gtid_start.seq);
            }

            dcb_printf(dcb, "\t\tCurrent GTID:                %lu-%lu-%lu\n",
                       session->gtid.domain, session->gtid.server_id,
                       session->gtid.seq);

            dcb_printf(dcb, "\t\t--------------------\n\n");
            session = session->next;
        }
        spinlock_release(&router_inst->lock);
    }
}

/**
 * Display router diagnostics
 *
 * @param instance  Instance of the router
 */
static json_t* diagnostics_json(const MXS_ROUTER *router)
{
    AVRO_INSTANCE *router_inst = (AVRO_INSTANCE *)router;

    json_t* rval = json_object();

    char pathbuf[PATH_MAX + 1];
    snprintf(pathbuf, sizeof(pathbuf), "%s/%s", router_inst->avrodir, AVRO_PROGRESS_FILE);

    json_object_set_new(rval, "infofile", json_string(pathbuf));
    json_object_set_new(rval, "avrodir", json_string(router_inst->avrodir));
    json_object_set_new(rval, "binlogdir", json_string(router_inst->binlogdir));
    json_object_set_new(rval, "binlog_name", json_string(router_inst->binlog_name));
    json_object_set_new(rval, "binlog_pos", json_integer(router_inst->current_pos));

    snprintf(pathbuf, sizeof(pathbuf), "%lu-%lu-%lu", router_inst->gtid.domain,
             router_inst->gtid.server_id, router_inst->gtid.seq);
    json_object_set_new(rval, "gtid", json_string(pathbuf));
    json_object_set_new(rval, "gtid_timestamp", json_integer(router_inst->gtid.timestamp));
    json_object_set_new(rval, "gtid_event_number", json_integer(router_inst->gtid.event_num));
    json_object_set_new(rval, "clients", json_integer(router_inst->stats.n_clients));

    if (router_inst->clients)
    {
        json_t* arr = json_array();
        spinlock_acquire(&router_inst->lock);

        for (AVRO_CLIENT *session = router_inst->clients; session; session = session->next)
        {
            json_t* client = json_object();
            json_object_set_new(client, "uuid", json_string(session->uuid));
            json_object_set_new(client, "host", json_string(session->dcb->remote));
            json_object_set_new(client, "port", json_integer(dcb_get_port(session->dcb)));
            json_object_set_new(client, "user", json_string(session->dcb->user));
            json_object_set_new(client, "format", json_string(avro_client_ouput[session->format]));
            json_object_set_new(client, "state", json_string(avro_client_states[session->state]));
            json_object_set_new(client, "avrofile", json_string(session->avro_binfile));
            json_object_set_new(client, "avrofile_last_block",
                                json_integer(session->avro_file.blocks_read));
            json_object_set_new(client, "avrofile_last_record",
                                json_integer(session->avro_file.records_read));

            if (session->gtid_start.domain > 0 || session->gtid_start.server_id > 0 ||
                session->gtid_start.seq > 0)
            {

                snprintf(pathbuf, sizeof(pathbuf), "%lu-%lu-%lu", session->gtid_start.domain,
                         session->gtid_start.server_id, session->gtid_start.seq);
                json_object_set_new(client, "requested_gtid", json_string(pathbuf));
            }
            snprintf(pathbuf, sizeof(pathbuf), "%lu-%lu-%lu", session->gtid.domain,
                     session->gtid.server_id, session->gtid.seq);
            json_object_set_new(client, "current_gtid", json_string(pathbuf));
            json_array_append_new(arr, client);
        }
        spinlock_release(&router_inst->lock);

        json_object_set_new(rval, "clients", arr);
    }

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

/*
static char *
extract_message(GWBUF *errpkt)
{
    char *rval;
    int len;

    len = EXTRACT24(errpkt->start);
    if ((rval = (char *) MXS_MALLOC(len)) == NULL)
    {
        return NULL;
    }
    memcpy(rval, (char *) (errpkt->start) + 7, 6);
    rval[6] = ' ';
    // message size is len - (1 byte field count + 2 bytes errno + 6 bytes status)
    memcpy(&rval[7], (char *) (errpkt->start) + 13, len - 9);
    rval[len - 2] = 0;
    return rval;
}
*/

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
 * The stats gathering function called from the housekeeper so that we
 * can get timed averages of binlog records shippped
 *
 * @param inst  The router instance
 */
/*
static void
stats_func(void *inst)
{
    AVRO_INSTANCE *router = (AVRO_INSTANCE *) inst;
    AVRO_CLIENT *client;

    router->stats.minavgs[router->stats.minno++]
        = router->stats.n_binlogs - router->stats.lastsample;
    router->stats.lastsample = router->stats.n_binlogs;
    if (router->stats.minno == AVRO_NSTATS_MINUTES)
    {
        router->stats.minno = 0;
    }

    spinlock_acquire(&router->lock);
    client = router->clients;
    while (client)
    {
        client->stats.minavgs[client->stats.minno++]
            = client->stats.n_events - client->stats.lastsample;
        client->stats.lastsample = client->stats.n_events;
        if (client->stats.minno == AVRO_NSTATS_MINUTES)
        {
            client->stats.minno = 0;
        }
        client = client->next;
    }
    spinlock_release(&router->lock);
}
*/

/**
 * Conversion task: MySQL binlogs to AVRO files
 */
void converter_func(void* data)
{
    AVRO_INSTANCE* router = (AVRO_INSTANCE*) data;
    bool ok = true;
    avro_binlog_end_t binlog_end = AVRO_OK;

    while (!router->service->svc_do_shutdown && ok && binlog_end == AVRO_OK)
    {
        uint64_t start_pos = router->current_pos;
        char binlog_name[BINLOG_FNAMELEN + 1];
        strcpy(binlog_name, router->binlog_name);

        if (avro_open_binlog(router->binlogdir, router->binlog_name, &router->binlog_fd))
        {
            binlog_end = avro_read_all_events(router);

            if (router->current_pos != start_pos || strcmp(binlog_name, router->binlog_name) != 0)
            {
                /** We processed some data, reset the conversion task delay */
                router->task_delay = 1;

                /** Update the GTID index */
                avro_update_index(router);
            }

            avro_close_binlog(router->binlog_fd);
        }
        else
        {
            binlog_end = AVRO_BINLOG_ERROR;
        }
    }

    /** We reached end of file, flush unwritten records to disk */
    if (router->task_delay == 1)
    {
        avro_flush_all_tables(router, AVROROUTER_FLUSH);
        avro_save_conversion_state(router);
    }

    if (binlog_end == AVRO_LAST_FILE)
    {
        router->task_delay = MXS_MIN(router->task_delay + 1, AVRO_TASK_DELAY_MAX);
        if (conversion_task_ctl(router, true))
        {
            MXS_INFO("Stopped processing file %s at position %lu. Waiting until"
                     " more data is written before continuing. Next check in %d seconds.",
                     router->binlog_name, router->current_pos, router->task_delay);
        }
    }
}

/**
 * @brief Ensure directory exists and is writable
 *
 * TODO: Move this as a function in the core
 *
 * @param path Path to directory
 * @param mode One of O_RDONLY, O_WRONLY or O_RDWR
 * @return True if directory exists and can be opened with @p mode permission
 */
static bool ensure_dir_ok(const char* path, int mode)
{
    bool rval = false;

    if (path)
    {
        char resolved[PATH_MAX + 1];
        const char *rp = realpath(path, resolved);

        if (rp == NULL && errno == ENOENT)
        {
            rp = path;
        }

        if (rp)
        {
            /** Make sure the directory exists */
            if (mkdir(rp, 0774) == 0 || errno == EEXIST)
            {
                if (access(rp, mode) == 0)
                {
                    rval = true;
                }
                else
                {
                    MXS_ERROR("Failed to access directory '%s': %d, %s", rp,
                              errno, mxs_strerror(errno));
                }
            }
            else
            {
                MXS_ERROR("Failed to create directory '%s': %d, %s", rp,
                          errno, mxs_strerror(errno));
            }
        }
        else
        {
            MXS_ERROR("Failed to resolve real path name for '%s': %d, %s", path,
                      errno, mxs_strerror(errno));
        }
    }

    return rval;
}
