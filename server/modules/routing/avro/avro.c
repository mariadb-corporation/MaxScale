/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl.
 *
 * Change Date: 2019-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * @file avro.c - Avro router, allows MaxScale to act as an intermediary for
 * MySQL replication binlog files and AVRO binary files
 *
 * @verbatim
 * Revision History
 *
 * Date         Who                   Description
 * 25/02/2016   Massimiliano Pinto    Initial implementation
 *
 * @endverbatim
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <service.h>
#include <server.h>
#include <router.h>
#include <atomic.h>
#include <spinlock.h>
#include <dcb.h>
#include <spinlock.h>
#include <housekeeper.h>
#include <time.h>

#include <skygw_types.h>
#include <skygw_utils.h>
#include <log_manager.h>

#include <mysql_client_server_protocol.h>
#include <ini.h>
#include <sys/stat.h>

#include <avrorouter.h>
#include <random_jkiss.h>
#include <binlog_common.h>
#include <avro/errors.h>

#ifndef BINLOG_NAMEFMT
#define BINLOG_NAMEFMT      "%s.%06d"
#endif

#define AVRO_TASK_DELAY_MAX 15

static char *version_str = "V1.0.0";
static const char* avro_task_name = "binlog_to_avro";
static const char* index_task_name = "avro_indexing";
static const char* avro_index_name = "avro.index";

/** For detection of CREATE/ALTER TABLE statements */
static const char* create_table_regex =
    "(?i)create[a-z0-9[:space:]_]+table";
static const char* alter_table_regex =
    "(?i)alter[[:space:]]+table.*column";

/* The router entry points */
static ROUTER *createInstance(SERVICE *service, char **options);
static void *newSession(ROUTER *instance, SESSION *session);
static void closeSession(ROUTER *instance, void *router_session);
static void freeSession(ROUTER *instance, void *router_session);
static int routeQuery(ROUTER *instance, void *router_session, GWBUF *queue);
static void diagnostics(ROUTER *instance, DCB *dcb);
static void clientReply(ROUTER *instance, void *router_session, GWBUF *queue,
                        DCB *backend_dcb);
static void errorReply(ROUTER *instance, void *router_session, GWBUF *message,
                       DCB *backend_dcb, error_action_t action, bool *succp);
static int getCapabilities();
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

/** The module object definition */
static ROUTER_OBJECT MyObject =
{
    createInstance,
    newSession,
    closeSession,
    freeSession,
    routeQuery,
    diagnostics,
    clientReply,
    errorReply,
    getCapabilities
};

static SPINLOCK instlock;
static AVRO_INSTANCE *instances;

/**
 * Implementation of the mandatory version entry point
 *
 * @return version string of the module
 */
char *
version()
{
    return version_str;
}

/**
 * The module initialisation routine, called when the module
 * is first loaded.
 */
void
ModuleInit()
{
    MXS_NOTICE("Initialized avrorouter module %s.\n", version_str);
    spinlock_init(&instlock);
    instances = NULL;
}

/**
 * The module entry point routine. It is this routine that
 * must populate the structure that is referred to as the
 * "module object", this is a structure with the set of
 * external entry points for this module.
 *
 * @return The module object
 */
ROUTER_OBJECT *
GetModuleObject()
{
    return &MyObject;
}

/**
 * @brief Safe hashtable key freeing function
 *
 * This function conforms to the HASHMEMORYFN type by returning a NULL pointer
 *
 * @param data Data to free
 * @return Always NULL
 */
void* safe_key_free(void *data)
{
    free(data);
    return NULL;
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

static void add_conversion_task(AVRO_INSTANCE *inst)
{
    char tasknm[strlen(avro_task_name) + strlen(inst->service->name) + 2];
    snprintf(tasknm, sizeof(tasknm), "%s-%s", inst->service->name, avro_task_name);
    if (hktask_oneshot(tasknm, converter_func, inst, inst->task_delay) == 0)
    {
        MXS_ERROR("Failed to add binlog to Avro conversion task to housekeeper.");
    }
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
void read_source_service_options(AVRO_INSTANCE *inst, const char** options)
{
    if (options)
    {
        for (int i = 0; options[i]; i++)
        {
            char option[strlen(options[i]) + 1];
            strncpy(option, options[i], sizeof(option));

            char *value = strchr(option, '=');
            if (value)
            {
                *value++ = '\0';
                value = trim(value);

                if (strcmp(option, "binlogdir") == 0)
                {
                    inst->binlogdir = strdup(value);
                    MXS_INFO("Reading MySQL binlog files from %s", inst->binlogdir);
                }
                else if (strcmp(option, "filestem") == 0)
                {
                    inst->fileroot = strdup(value);
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
static ROUTER *
createInstance(SERVICE *service, char **options)
{
    AVRO_INSTANCE *inst;
    int i;

    if ((inst = calloc(1, sizeof(AVRO_INSTANCE))) == NULL)
    {
        MXS_ERROR("%s: Error: failed to allocate memory for router instance.",
                  service->name);
        return NULL;
    }

    memset(&inst->stats, 0, sizeof(AVRO_ROUTER_STATS));
    spinlock_init(&inst->lock);
    spinlock_init(&inst->fileslock);
    inst->service = service;
    inst->binlog_fd = -1;
    inst->binlogdir = NULL;
    inst->avrodir = NULL;
    inst->current_pos = 4;
    inst->binlog_position = 4;
    inst->clients = NULL;
    inst->next = NULL;
    inst->lastEventTimestamp = 0;
    inst->binlog_position = 0;
    inst->task_delay = 1;
    inst->row_count = 0;
    inst->trx_count = 0;
    inst->row_target = AVRO_DEFAULT_BLOCK_ROW_COUNT;
    inst->trx_target = AVRO_DEFAULT_BLOCK_TRX_COUNT;
    inst->block_size = 0;
    inst->gtid.domain = 0;
    inst->gtid.event_num = 0;
    inst->gtid.seq = 0;
    inst->gtid.server_id = 0;
    inst->gtid.timestamp = 0;
    int first_file = 1;
    bool err = false;

    CONFIG_PARAMETER *param = config_get_param(service->svc_config_param, "source");
    if (param)
    {
        SERVICE *source = service_find(param->value);
        if (source)
        {
            if (strcmp(source->routerModule, "binlogrouter") == 0)
            {
                MXS_NOTICE("[%s] Using configuration options from service '%s'.",
                           service->name, source->name);
                read_source_service_options(inst, (const char**)source->routerOptions);
            }
            else
            {
                MXS_ERROR("[%s] Service '%s' uses router module '%s' instead of"
                          " 'binlogrouter'.", service->name, source->name,
                          source->routerModule);
                err = true;
            }
        }
        else
        {
            MXS_ERROR("[%s] No service '%s' found in configuration.",
                      service->name, param->value);
            err = true;
        }
    }

    if (options)
    {
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
                    free(inst->binlogdir);
                    inst->binlogdir = strdup(value);
                    MXS_INFO("Reading MySQL binlog files from %s", inst->binlogdir);
                }
                else if (strcmp(options[i], "avrodir") == 0)
                {
                    inst->avrodir = strdup(value);
                    MXS_INFO("AVRO files stored in %s", inst->avrodir);
                }
                else if (strcmp(options[i], "filestem") == 0)
                {
                    free(inst->fileroot);
                    inst->fileroot = strdup(value);
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
                    first_file = MAX(1, atoi(value));
                }
                else if (strcmp(options[i], "block_size") == 0)
                {
                    inst->block_size = atoi(value);
                }
                else
                {
                    MXS_WARNING("[avrorouter] Unknown router option: '%s'", options[i]);
                    err = true;
                }
            }
            else
            {
                MXS_WARNING("[avrorouter] Unknown router option: '%s'", options[i]);
                err = true;
            }
        }
    }

    if (inst->binlogdir == NULL)
    {
        MXS_ERROR("No 'binlogdir' option found in source service or in router_options.");
        err = true;
    }
    else if (!ensure_dir_ok(inst->binlogdir, R_OK))
    {
        MXS_ERROR("Access to binary log directory is not possible.");
        err = true;
    }
    else
    {
        if (inst->fileroot == NULL)
        {
            MXS_NOTICE("[%s] No 'filestem' option specified, using default binlog name '%s'.",
                       service->name, BINLOG_NAME_ROOT);
            inst->fileroot = strdup(BINLOG_NAME_ROOT);
        }

        /** Use the binlogdir as the default if no avrodir is specified. */
        if (inst->avrodir == NULL && inst->binlogdir)
        {
            inst->avrodir = strdup(inst->binlogdir);
        }

        if (ensure_dir_ok(inst->avrodir, W_OK))
        {
            MXS_NOTICE("[%s] Avro files stored at: %s", service->name, inst->avrodir);
        }
        else
        {
            MXS_ERROR("Access to Avro file directory is not possible.");
            err = true;
        }
    }

    snprintf(inst->binlog_name, sizeof(inst->binlog_name), BINLOG_NAMEFMT, inst->fileroot, first_file);
    inst->prevbinlog[0] = '\0';

    if ((inst->table_maps = hashtable_alloc(1000, simple_str_hash, strcmp)) &&
        (inst->open_tables = hashtable_alloc(1000, simple_str_hash, strcmp)) &&
        (inst->created_tables = hashtable_alloc(1000, simple_str_hash, strcmp)))
    {
        hashtable_memory_fns(inst->table_maps, (HASHMEMORYFN)strdup, NULL,
                             safe_key_free, (HASHMEMORYFN)table_map_free);
        hashtable_memory_fns(inst->open_tables, (HASHMEMORYFN)strdup, NULL,
                             safe_key_free, (HASHMEMORYFN)avro_table_free);
        hashtable_memory_fns(inst->created_tables, (HASHMEMORYFN)strdup, NULL,
                             safe_key_free, (HASHMEMORYFN)table_create_free);
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
        free(inst->avrodir);
        free(inst->binlogdir);
        free(inst->fileroot);
        free(inst);
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
    add_conversion_task(inst);

    MXS_INFO("AVRO: current MySQL binlog file is %s, pos is %lu\n",
             inst->binlog_name, inst->current_pos);

    return (ROUTER *) inst;
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
static void *
newSession(ROUTER *instance, SESSION *session)
{
    AVRO_INSTANCE *inst = (AVRO_INSTANCE *) instance;
    AVRO_CLIENT *client;

    MXS_DEBUG("avrorouter: %lu [newSession] new router session with "
              "session %p, and inst %p.", pthread_self(), session, inst);

    if ((client = (AVRO_CLIENT *) calloc(1, sizeof(AVRO_CLIENT))) == NULL)
    {
        MXS_ERROR("Insufficient memory to create new client session for AVRO router");
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
static void freeSession(ROUTER* router_instance, void* router_client_ses)
{
    AVRO_INSTANCE *router = (AVRO_INSTANCE *) router_instance;
    AVRO_CLIENT *client = (AVRO_CLIENT *) router_client_ses;
    int prev_val;

    prev_val = atomic_add(&router->stats.n_clients, -1);
    ss_dassert(prev_val > 0);
    (void) prev_val;

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

    free(client);
}

/**
 * Close a session with the router, this is the mechanism
 * by which a router may cleanup data structure etc.
 *
 * @param instance          The router instance data
 * @param router_session    The session being closed
 */
static void closeSession(ROUTER *instance, void *router_session)
{
    AVRO_INSTANCE *router = (AVRO_INSTANCE *) instance;
    AVRO_CLIENT *client = (AVRO_CLIENT *) router_session;

    CHK_CLIENT_RSES(client);

    spinlock_acquire(&client->catch_lock);
    spinlock_acquire(&client->file_lock);

    client->state = AVRO_CLIENT_UNREGISTERED;

    spinlock_release(&client->file_lock);
    spinlock_release(&client->catch_lock);

    /* decrease server registered slaves counter */
    atomic_add(&router->stats.n_clients, -1);
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
routeQuery(ROUTER *instance, void *router_session, GWBUF *queue)
{
    AVRO_INSTANCE *router = (AVRO_INSTANCE *) instance;
    AVRO_CLIENT *client = (AVRO_CLIENT *) router_session;

    return avro_client_handle_request(router, client, queue);
}

/* Not used
static char *event_names[] =
{
    "Invalid", "Start Event V3", "Query Event", "Stop Event", "Rotate Event",
    "Integer Session Variable", "Load Event", "Slave Event", "Create File Event",
    "Append Block Event", "Exec Load Event", "Delete File Event",
    "New Load Event", "Rand Event", "User Variable Event", "Format Description Event",
    "Transaction ID Event (2 Phase Commit)", "Begin Load Query Event",
    "Execute Load Query Event", "Table Map Event", "Write Rows Event (v0)",
    "Update Rows Event (v0)", "Delete Rows Event (v0)", "Write Rows Event (v1)",
    "Update Rows Event (v1)", "Delete Rows Event (v1)", "Incident Event",
    "Heartbeat Event", "Ignorable Event", "Rows Query Event", "Write Rows Event (v2)",
    "Update Rows Event (v2)", "Delete Rows Event (v2)", "GTID Event",
    "Anonymous GTID Event", "Previous GTIDS Event"
};
*/

/* Not used
// New MariaDB event numbers starts from 0xa0
static char *event_names_mariadb10[] =
{
    "Annotate Rows Event",
    "Binlog Checkpoint Event",
    "GTID Event",
    "GTID List Event"
};
*/

/**
 * Display an entry from the spinlock statistics data
 *
 * @param   dcb The DCB to print to
 * @param   desc    Description of the statistic
 * @param   value   The statistic value
 */
static void
spin_reporter(void *dcb, char *desc, int value)
{
    dcb_printf((DCB *) dcb, "\t\t%-35s  %d\n", desc, value);
}

/**
 * Display router diagnostics
 *
 * @param instance  Instance of the router
 * @param dcb       DCB to send diagnostics to
 */
static void
diagnostics(ROUTER *router, DCB *dcb)
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
            dcb_printf(dcb, "\t\tClient_host_port:            %s:%d\n",
                       session->dcb->remote, ntohs((session->dcb->ipv4).sin_port));
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

            // TODO: Add real value for this
            //dcb_printf(dcb, "\t\tAvro Transaction ID:         %u\n", 0);
            // TODO: Add real value for this
            //dcb_printf(dcb, "\t\tAvro N.MaxTransactions:          %u\n", 0);

#if SPINLOCK_PROFILE
            dcb_printf(dcb, "\tSpinlock statistics (catch_lock):\n");
            spinlock_stats(&session->catch_lock, spin_reporter, dcb);
            dcb_printf(dcb, "\tSpinlock statistics (rses_lock):\n");
            spinlock_stats(&session->file_lock, spin_reporter, dcb);
#endif
            dcb_printf(dcb, "\t\t--------------------\n\n");
            session = session->next;
        }
        spinlock_release(&router_inst->lock);
    }
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
clientReply(ROUTER *instance, void *router_session, GWBUF *queue, DCB *backend_dcb)
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
    if ((rval = (char *) malloc(len)) == NULL)
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
errorReply(ROUTER *instance, void *router_session, GWBUF *message, DCB *backend_dcb, error_action_t action,
           bool *succp)
{
    /** We should never end up here */
    ss_dassert(false);
}

static int getCapabilities()
{
    return RCAP_TYPE_NO_RSESSION;
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
    while (ok && binlog_end == AVRO_OK)
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
        avro_flush_all_tables(router);
        avro_save_conversion_state(router);
    }

    if (binlog_end == AVRO_LAST_FILE)
    {
        router->task_delay = MIN(router->task_delay + 1, AVRO_TASK_DELAY_MAX);
        add_conversion_task(router);
        MXS_INFO("Stopped processing file %s at position %lu. Waiting until"
                 " more data is written before continuing. Next check in %d seconds.",
                 router->binlog_name, router->current_pos, router->task_delay);
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
        char err[STRERROR_BUFLEN];
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
                              errno, strerror_r(errno, err, sizeof(err)));
                }
            }
            else
            {
                MXS_ERROR("Failed to create directory '%s': %d, %s", rp,
                          errno, strerror_r(errno, err, sizeof(err)));
            }
        }
        else
        {
            MXS_ERROR("Failed to resolve real path name for '%s': %d, %s", path,
                      errno, strerror_r(errno, err, sizeof(err)));
        }
    }

    return rval;
}
