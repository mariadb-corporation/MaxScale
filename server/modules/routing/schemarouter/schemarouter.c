/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2019-07-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "schemarouter.h"

#include <stdio.h>
#include <strings.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <maxscale/router.h>
#include "sharding_common.h"
#include <maxscale/secrets.h>
#include <mysql.h>
#include <maxscale/log_manager.h>
#include <maxscale/query_classifier.h>
#include <maxscale/dcb.h>
#include <maxscale/spinlock.h>
#include <maxscale/modinfo.h>
#include <maxscale/modutil.h>
#include <maxscale/protocol/mysql.h>
#include <maxscale/alloc.h>
#include <maxscale/poll.h>
#include <pcre.h>

#define DEFAULT_REFRESH_INTERVAL "300"

/** Size of the hashtable used to store ignored databases */
#define SCHEMAROUTER_HASHSIZE 100

/** Hashtable size for the per user shard maps */
#define SCHEMAROUTER_USERHASH_SIZE 10

/**
 * @file schemarouter.c The entry points for the simple sharding
 * router module.
 *.
 * @verbatim
 * Revision History
 *
 *  Date        Who                           Description
 *  01/12/2014  Vilho Raatikka/Markus Mäkelä  Initial implementation
 *  09/09/2015  Martin Brampton               Modify error handler
 *  25/09/2015  Martin Brampton               Block callback processing when no router session in the DCB
 *
 * @endverbatim
 */

static MXS_ROUTER* createInstance(SERVICE *service, char **options);
static MXS_ROUTER_SESSION* newSession(MXS_ROUTER *instance, MXS_SESSION *session);
static void closeSession(MXS_ROUTER *instance, MXS_ROUTER_SESSION *session);
static void freeSession(MXS_ROUTER *instance, MXS_ROUTER_SESSION *session);
static int routeQuery(MXS_ROUTER *instance, MXS_ROUTER_SESSION *session, GWBUF *queue);
static void diagnostic(MXS_ROUTER *instance, DCB *dcb);

static void clientReply(MXS_ROUTER* instance,
                        MXS_ROUTER_SESSION* router_session,
                        GWBUF* queue,
                        DCB* backend_dcb);

static void handleError(MXS_ROUTER* instance,
                        MXS_ROUTER_SESSION* router_session,
                        GWBUF* errmsgbuf,
                        DCB* backend_dcb,
                        mxs_error_action_t action,
                        bool* succp);
static backend_ref_t* get_bref_from_dcb(ROUTER_CLIENT_SES* rses, DCB* dcb);

static route_target_t get_shard_route_target(qc_query_type_t qtype,
                                             bool            trx_active,
                                             HINT*           hint);

static uint64_t getCapabilities(MXS_ROUTER* instance);

static bool connect_backend_servers(backend_ref_t*   backend_ref,
                                    int              router_nservers,
                                    MXS_SESSION*     session,
                                    ROUTER_INSTANCE* router);

static bool get_shard_dcb(DCB**              dcb,
                          ROUTER_CLIENT_SES* rses,
                          char*              name);

static bool rses_begin_locked_router_action(ROUTER_CLIENT_SES* rses);
static void rses_end_locked_router_action(ROUTER_CLIENT_SES* rses);
static void mysql_sescmd_done(mysql_sescmd_t* sescmd);
static mysql_sescmd_t* mysql_sescmd_init(rses_property_t*   rses_prop,
                                         GWBUF*             sescmd_buf,
                                         unsigned char      packet_type,
                                         ROUTER_CLIENT_SES* rses);
static rses_property_t* mysql_sescmd_get_property(mysql_sescmd_t* scmd);
static rses_property_t* rses_property_init(rses_property_type_t prop_type);
static void rses_property_add(ROUTER_CLIENT_SES* rses,
                              rses_property_t*   prop);
static void rses_property_done(rses_property_t* prop);
static mysql_sescmd_t* rses_property_get_sescmd(rses_property_t* prop);
static bool execute_sescmd_history(backend_ref_t* bref);
static bool execute_sescmd_in_backend(backend_ref_t* backend_ref);
static void sescmd_cursor_reset(sescmd_cursor_t* scur);
static bool sescmd_cursor_history_empty(sescmd_cursor_t* scur);
static void sescmd_cursor_set_active(sescmd_cursor_t* sescmd_cursor,
                                     bool             value);
static bool sescmd_cursor_is_active(sescmd_cursor_t* sescmd_cursor);
static GWBUF* sescmd_cursor_clone_querybuf(sescmd_cursor_t* scur);
static mysql_sescmd_t* sescmd_cursor_get_command(sescmd_cursor_t* scur);
static bool sescmd_cursor_next(sescmd_cursor_t* scur);
static GWBUF* sescmd_cursor_process_replies(GWBUF* replybuf, backend_ref_t* bref);
static void tracelog_routed_query(ROUTER_CLIENT_SES* rses,
                                  char*              funcname,
                                  backend_ref_t*     bref,
                                  GWBUF*             buf);
static bool route_session_write(ROUTER_CLIENT_SES* router_client_ses,
                                GWBUF*             querybuf,
                                ROUTER_INSTANCE*   inst,
                                unsigned char      packet_type,
                                qc_query_type_t    qtype);
static void bref_clear_state(backend_ref_t* bref, bref_state_t state);
static void bref_set_state(backend_ref_t*   bref, bref_state_t state);
static sescmd_cursor_t* backend_ref_get_sescmd_cursor (backend_ref_t* bref);
static int  router_handle_state_switch(DCB* dcb, DCB_REASON reason, void* data);
static bool handle_error_new_connection(ROUTER_INSTANCE*   inst,
                                        ROUTER_CLIENT_SES* rses,
                                        DCB*               backend_dcb,
                                        GWBUF*             errmsg);
static void handle_error_reply_client(MXS_SESSION*       ses,
                                      ROUTER_CLIENT_SES* rses,
                                      DCB*               backend_dcb,
                                      GWBUF*             errmsg);

static SPINLOCK instlock;
static ROUTER_INSTANCE* instances;

bool detect_show_shards(GWBUF* query);
int process_show_shards(ROUTER_CLIENT_SES* rses);
static int hashkeyfun(const void* key);
static int hashcmpfun(const void *, const void *);

void write_error_to_client(DCB* dcb, int errnum, const char* mysqlstate, const char* errmsg);
int inspect_backend_mapping_states(ROUTER_CLIENT_SES *router_cli_ses,
                                   backend_ref_t *bref,
                                   GWBUF** wbuf);
bool handle_default_db(ROUTER_CLIENT_SES *router_cli_ses);
void route_queued_query(ROUTER_CLIENT_SES *router_cli_ses);
void synchronize_shard_map(ROUTER_CLIENT_SES *client);

static int hashkeyfun(const void* key)
{
    if (key == NULL)
    {
        return 0;
    }
    int hash = 0, c = 0;
    const char* ptr = (const char*)key;
    while ((c = *ptr++))
    {
        hash = c + (hash << 6) + (hash << 16) - hash;
    }

    return hash;
}

static int hashcmpfun(const void* v1, const void* v2)
{
    const char* i1 = (const char*) v1;
    const char* i2 = (const char*) v2;

    return strcmp(i1, i2);
}

void keyfreefun(void* data)
{
    MXS_FREE(data);
}

/**
 * Allocate a shard map and initialize it.
 * @return Pointer to new shard_map_t or NULL if memory allocation failed
 */
shard_map_t* shard_map_alloc()
{
    shard_map_t *rval = (shard_map_t*) MXS_MALLOC(sizeof(shard_map_t));

    if (rval)
    {
        if ((rval->hash = hashtable_alloc(SCHEMAROUTER_HASHSIZE, hashkeyfun, hashcmpfun)))
        {
            HASHCOPYFN kcopy = (HASHCOPYFN)strdup;
            hashtable_memory_fns(rval->hash, kcopy, kcopy, keyfreefun, keyfreefun);
            spinlock_init(&rval->lock);
            rval->last_updated = 0;
            rval->state = SHMAP_UNINIT;
        }
        else
        {
            MXS_FREE(rval);
            rval = NULL;
        }
    }
    return rval;
}

/**
 * Convert a length encoded string into a C string.
 * @param data Pointer to the first byte of the string
 * @return Pointer to the newly allocated string or NULL if the value is NULL or an error occurred
 */
char* get_lenenc_str(void* data)
{
    unsigned char* ptr = (unsigned char*)data;
    char* rval;
    uintptr_t size;
    long offset;

    if (data == NULL)
    {
        return NULL;
    }

    if (*ptr < 251)
    {
        size = (uintptr_t) * ptr;
        offset = 1;
    }
    else
    {
        switch (*(ptr))
        {
        case 0xfb:
            return NULL;
        case 0xfc:
            size = *(ptr + 1) + (*(ptr + 2) << 8);
            offset = 2;
            break;
        case 0xfd:
            size = *ptr + (*(ptr + 2) << 8) + (*(ptr + 3) << 16);
            offset = 3;
            break;
        case 0xfe:
            size = *ptr + ((*(ptr + 2) << 8)) + (*(ptr + 3) << 16) +
                   (*(ptr + 4) << 24) + ((uintptr_t) * (ptr + 5) << 32) +
                   ((uintptr_t) * (ptr + 6) << 40) +
                   ((uintptr_t) * (ptr + 7) << 48) + ((uintptr_t) * (ptr + 8) << 56);
            offset = 8;
            break;
        default:
            return NULL;
        }
    }

    rval = MXS_MALLOC(sizeof(char) * (size + 1));
    if (rval)
    {
        memcpy(rval, ptr + offset, size);
        memset(rval + size, 0, 1);

    }
    return rval;
}

/**
 * Parses a response set to a SHOW DATABASES query and inserts them into the
 * router client session's database hashtable. The name of the database is used
 * as the key and the unique name of the server is the value. The function
 * currently supports only result sets that span a single SQL packet.
 * @param rses Router client session
 * @param target Target server where the database is
 * @param buf GWBUF containing the result set
 * @return 1 if a complete response was received, 0 if a partial response was received
 * and -1 if a database was found on more than one server.
 */
showdb_response_t parse_showdb_response(ROUTER_CLIENT_SES* rses, backend_ref_t* bref, GWBUF** buffer)
{
    unsigned char* ptr;
    char* target = bref->bref_backend->server->unique_name;
    GWBUF* buf;
    bool duplicate_found = false;
    showdb_response_t rval = SHOWDB_PARTIAL_RESPONSE;

    if (buffer == NULL || *buffer == NULL)
    {
        return SHOWDB_FATAL_ERROR;
    }

    /** TODO: Don't make the buffer contiguous but process it as a buffer chain */
    *buffer = gwbuf_make_contiguous(*buffer);
    buf = modutil_get_complete_packets(buffer);

    if (buf == NULL)
    {
        return SHOWDB_PARTIAL_RESPONSE;
    }

    ptr = (unsigned char*) buf->start;

    if (PTR_IS_ERR(ptr))
    {
        MXS_INFO("SHOW DATABASES returned an error.");
        gwbuf_free(buf);
        return SHOWDB_FATAL_ERROR;
    }

    if (bref->n_mapping_eof == 0)
    {
        /** Skip column definitions */
        while (ptr < (unsigned char*) buf->end && !PTR_IS_EOF(ptr))
        {
            ptr += gw_mysql_get_byte3(ptr) + 4;
        }

        if (ptr >= (unsigned char*) buf->end)
        {
            MXS_INFO("Malformed packet for SHOW DATABASES.");
            *buffer = gwbuf_append(buf, *buffer);
            return SHOWDB_FATAL_ERROR;
        }

        atomic_add(&bref->n_mapping_eof, 1);
        /** Skip first EOF packet */
        ptr += gw_mysql_get_byte3(ptr) + 4;
    }

    spinlock_acquire(&rses->shardmap->lock);
    while (ptr < (unsigned char*) buf->end && !PTR_IS_EOF(ptr))
    {
        int payloadlen = gw_mysql_get_byte3(ptr);
        int packetlen = payloadlen + 4;
        char* data = get_lenenc_str(ptr + 4);

        if (data)
        {
            if (hashtable_add(rses->shardmap->hash, data, target))
            {
                MXS_INFO("<%s, %s>", target, data);
            }
            else
            {
                if (!(hashtable_fetch(rses->router->ignored_dbs, data) ||
                      (rses->router->ignore_regex &&
                       pcre2_match(rses->router->ignore_regex, (PCRE2_SPTR)data,
                                   PCRE2_ZERO_TERMINATED, 0, 0,
                                   rses->router->ignore_match_data, NULL) >= 0)))
                {
                    duplicate_found = true;
                    MXS_ERROR("Database '%s' found on servers '%s' and '%s' for user %s@%s.",
                              data, target,
                              (char*)hashtable_fetch(rses->shardmap->hash, data),
                              rses->rses_client_dcb->user,
                              rses->rses_client_dcb->remote);
                }
                else if (rses->router->preferred_server &&
                         strcmp(target, rses->router->preferred_server->unique_name) == 0)
                {
                    /** In conflict situations, use the preferred server */
                    MXS_INFO("Forcing location of '%s' from '%s' to ''%s",
                             data, (char*)hashtable_fetch(rses->shardmap->hash,
                                                          data), target);

                    hashtable_delete(rses->shardmap->hash, data);
                    hashtable_add(rses->shardmap->hash, data, target);
                }
            }
            MXS_FREE(data);
        }
        ptr += packetlen;
    }
    spinlock_release(&rses->shardmap->lock);

    if (ptr < (unsigned char*) buf->end && PTR_IS_EOF(ptr) && bref->n_mapping_eof == 1)
    {
        atomic_add(&bref->n_mapping_eof, 1);
        MXS_INFO("SHOW DATABASES fully received from %s.",
                 bref->bref_backend->server->unique_name);
    }
    else
    {
        MXS_INFO("SHOW DATABASES partially received from %s.",
                 bref->bref_backend->server->unique_name);
    }

    gwbuf_free(buf);

    if (duplicate_found)
    {
        rval = SHOWDB_DUPLICATE_DATABASES;
    }
    else if (bref->n_mapping_eof == 2)
    {
        rval = SHOWDB_FULL_RESPONSE;
    }

    return rval;
}

/**
 * Initiate the generation of the database hash table by sending a
 * SHOW DATABASES query to each valid backend server. This sets the session
 * into the mapping state where it queues further queries until all the database
 * servers have returned a result.
 * @param inst Router instance
 * @param session Router client session
 * @return 1 if all writes to backends were succesful and 0 if one or more errors occurred
 */
int gen_databaselist(ROUTER_INSTANCE* inst, ROUTER_CLIENT_SES* session)
{
    DCB* dcb;
    const char* query = "SHOW DATABASES";
    GWBUF *buffer, *clone;
    int i, rval = 0;
    unsigned int len;

    for (i = 0; i < session->rses_nbackends; i++)
    {
        session->rses_backend_ref[i].bref_mapped = false;
        session->rses_backend_ref[i].n_mapping_eof = 0;
    }

    session->init |= INIT_MAPPING;
    session->init &= ~INIT_UNINT;
    len = strlen(query) + 1;
    buffer = gwbuf_alloc(len + 4);
    *((unsigned char*)buffer->start) = len;
    *((unsigned char*)buffer->start + 1) = len >> 8;
    *((unsigned char*)buffer->start + 2) = len >> 16;
    *((unsigned char*)buffer->start + 3) = 0x0;
    *((unsigned char*)buffer->start + 4) = 0x03;
    memcpy(buffer->start + 5, query, strlen(query));

    for (i = 0; i < session->rses_nbackends; i++)
    {
        if (BREF_IS_IN_USE(&session->rses_backend_ref[i]) &&
            !BREF_IS_CLOSED(&session->rses_backend_ref[i]) &
            SERVER_IS_RUNNING(session->rses_backend_ref[i].bref_backend->server))
        {
            clone = gwbuf_clone(buffer);
            dcb = session->rses_backend_ref[i].bref_dcb;
            rval |= !dcb->func.write(dcb, clone);
            MXS_DEBUG("Wrote SHOW DATABASES to %s for session %p: returned %d",
                      session->rses_backend_ref[i].bref_backend->server->unique_name,
                      session->rses_client_dcb->session,
                      rval);
        }
    }
    gwbuf_free(buffer);
    return !rval;
}

/**
 * Check the hashtable for the right backend for this query.
 * @param router Router instance
 * @param client Client router session
 * @param buffer Query to inspect
 * @return Name of the backend or NULL if the query contains no known databases.
 */
char* get_shard_target_name(ROUTER_INSTANCE* router,
                            ROUTER_CLIENT_SES* client,
                            GWBUF* buffer,
                            qc_query_type_t qtype)
{
    int sz = 0, i, j;
    char** dbnms = NULL;
    char* rval = NULL, *query, *tmp = NULL;
    bool has_dbs = false; /**If the query targets any database other than the current one*/
    bool uses_implicit_databases = false;

    dbnms = qc_get_table_names(buffer, &sz, true);

    for (i = 0; i < sz; i++)
    {
        if (strchr(dbnms[i], '.') == NULL)
        {
            uses_implicit_databases = true;
        }
        MXS_FREE(dbnms[i]);
    }
    MXS_FREE(dbnms);

    HASHTABLE* ht = client->shardmap->hash;

    if (uses_implicit_databases)
    {
        MXS_INFO("Query implicitly uses the current database");
        return (char*)hashtable_fetch(ht, client->current_db);
    }

    dbnms = qc_get_database_names(buffer, &sz);

    if (sz > 0)
    {
        for (i = 0; i < sz; i++)
        {
            char* name;
            if ((name = (char*)hashtable_fetch(ht, dbnms[i])))
            {
                if (strcmp(dbnms[i], "information_schema") == 0 && rval == NULL)
                {
                    has_dbs = false;
                }
                else
                {
                    /** Warn about improper usage of the router */
                    if (rval && strcmp(name, rval) != 0)
                    {
                        MXS_ERROR("Query targets databases on servers '%s' and '%s'. "
                                  "Cross database queries across servers are not supported.",
                                  rval, name);
                    }
                    else if (rval == NULL)
                    {
                        rval = name;
                        has_dbs = true;
                        MXS_INFO("Query targets database '%s' on server '%s'", dbnms[i], rval);
                    }
                }
            }
            MXS_FREE(dbnms[i]);
        }
        MXS_FREE(dbnms);
    }

    /* Check if the query is a show tables query with a specific database */

    if (qc_query_is_type(qtype, QUERY_TYPE_SHOW_TABLES))
    {
        query = modutil_get_SQL(buffer);
        if ((tmp = strcasestr(query, "from")))
        {
            const char *delim = "` \n\t;";
            char *saved, *tok = strtok_r(tmp, delim, &saved);
            tok = strtok_r(NULL, delim, &saved);
            ss_dassert(tok != NULL);
            tmp = (char*) hashtable_fetch(ht, tok);

            if (tmp)
            {
                MXS_INFO("SHOW TABLES with specific database '%s' on server '%s'", tok, tmp);
            }
        }
        MXS_FREE(query);

        if (tmp == NULL)
        {
            rval = (char*) hashtable_fetch(ht, client->current_db);
            MXS_INFO("SHOW TABLES query, current database '%s' on server '%s'",
                     client->current_db, rval);
        }
        else
        {
            rval = tmp;
            has_dbs = true;
        }
    }
    else
    {
        if (buffer->hint && buffer->hint->type == HINT_ROUTE_TO_NAMED_SERVER)
        {
            for (i = 0; i < client->rses_nbackends; i++)
            {

                char *srvnm = client->rses_backend_ref[i].bref_backend->server->unique_name;
                if (strcmp(srvnm, buffer->hint->data) == 0)
                {
                    rval = srvnm;
                    MXS_INFO("Routing hint found (%s)", srvnm);
                }
            }
        }

        if (rval == NULL && !has_dbs && client->current_db[0] != '\0')
        {
            /**
             * If the target name has not been found and the session has an
             * active database, set is as the target
             */

            rval = (char*) hashtable_fetch(ht, client->current_db);
            if (rval)
            {
                MXS_INFO("Using active database '%s'", client->current_db);
            }
        }
    }

    return rval;
}

/**
 * Check if the backend is still running. If the backend is not running the
 * hashtable is updated with up-to-date values.
 * @param router Router instance
 * @param shard Shard to check
 * @return True if the backend server is running
 */
bool check_shard_status(ROUTER_INSTANCE* router, char* shard)
{
    for (SERVER_REF *ref = router->service->dbref; ref; ref = ref->next)
    {
        if (strcmp(ref->server->unique_name, shard) == 0 &&
            SERVER_IS_RUNNING(ref->server))
        {
            return true;
        }
    }

    return false;
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
    MXS_NOTICE("Initializing Schema Sharding Router.");
    spinlock_init(&instlock);
    instances = NULL;

    static MXS_ROUTER_OBJECT MyObject =
    {
        createInstance,
        newSession,
        closeSession,
        freeSession,
        routeQuery,
        diagnostic,
        clientReply,
        handleError,
        getCapabilities,
        NULL
    };

    static MXS_MODULE info =
    {
        MXS_MODULE_API_ROUTER,
        MXS_MODULE_BETA_RELEASE,
        MXS_ROUTER_VERSION,
        "A database sharding router for simple sharding",
        "V1.0.0",
        &MyObject,
        NULL, /* Process init. */
        NULL, /* Process finish. */
        NULL, /* Thread init. */
        NULL, /* Thread finish. */
        {
            {"ignore_databases", MXS_MODULE_PARAM_STRING},
            {"ignore_databases_regex", MXS_MODULE_PARAM_STRING},
            {"max_sescmd_history", MXS_MODULE_PARAM_COUNT, "0"},
            {"disable_sescmd_history", MXS_MODULE_PARAM_BOOL, "false"},
            {"refresh_databases", MXS_MODULE_PARAM_BOOL, "true"},
            {"refresh_interval", MXS_MODULE_PARAM_COUNT, DEFAULT_REFRESH_INTERVAL},
            {"debug", MXS_MODULE_PARAM_BOOL, "false"},
            {"preferred_server", MXS_MODULE_PARAM_SERVER},
            {MXS_END_MODULE_PARAMS}
        }
    };

    return &info;
}

/**
 * Create an instance of schemarouter router within the MaxScale.
 *
 *
 * @param service       The service this router is being create for
 * @param options       The options for this query router
 *
 * @return NULL in failure, pointer to router in success.
 */
static MXS_ROUTER* createInstance(SERVICE *service, char **options)
{
    ROUTER_INSTANCE* router;
    MXS_CONFIG_PARAMETER* conf;
    MXS_CONFIG_PARAMETER* param;

    if ((router = MXS_CALLOC(1, sizeof(ROUTER_INSTANCE))) == NULL)
    {
        return NULL;
    }

    if ((router->ignored_dbs = hashtable_alloc(SCHEMAROUTER_HASHSIZE, hashkeyfun, hashcmpfun)) == NULL)
    {
        MXS_ERROR("Memory allocation failed when allocating schemarouter database ignore list.");
        MXS_FREE(router);
        return NULL;
    }

    hashtable_memory_fns(router->ignored_dbs, hashtable_item_strdup, NULL, hashtable_item_free, NULL);

    if ((router->shard_maps = hashtable_alloc(SCHEMAROUTER_USERHASH_SIZE, hashkeyfun, hashcmpfun)) == NULL)
    {
        MXS_ERROR("Memory allocation failed when allocating schemarouter database ignore list.");
        hashtable_free(router->ignored_dbs);
        MXS_FREE(router);
        return NULL;
    }

    hashtable_memory_fns(router->shard_maps, hashtable_item_strdup, NULL, keyfreefun, NULL);

    /** Add default system databases to ignore */
    hashtable_add(router->ignored_dbs, "mysql", "");
    hashtable_add(router->ignored_dbs, "information_schema", "");
    hashtable_add(router->ignored_dbs, "performance_schema", "");
    router->service = service;
    router->schemarouter_config.max_sescmd_hist = 0;
    router->schemarouter_config.last_refresh = time(NULL);
    router->stats.longest_sescmd = 0;
    router->stats.n_hist_exceeded = 0;
    router->stats.n_queries = 0;
    router->stats.n_sescmd = 0;
    router->stats.ses_longest = 0;
    router->stats.ses_shortest = (double)((unsigned long)(~0));
    spinlock_init(&router->lock);

    conf = service->svc_config_param;

    router->schemarouter_config.refresh_databases = config_get_bool(conf, "refresh_databases");
    router->schemarouter_config.refresh_min_interval = config_get_integer(conf, "refresh_interval");
    router->schemarouter_config.max_sescmd_hist = config_get_integer(conf, "max_sescmd_history");
    router->schemarouter_config.disable_sescmd_hist = config_get_bool(conf, "disable_sescmd_history");
    router->schemarouter_config.debug = config_get_bool(conf, "debug");
    router->preferred_server = config_get_server(conf, "preferred_server");

    if ((config_get_param(conf, "auth_all_servers")) == NULL)
    {
        MXS_NOTICE("Authentication data is fetched from all servers. To disable this "
                   "add 'auth_all_servers=0' to the service.");
        service->users_from_all = true;
    }

    if ((param = config_get_param(conf, "ignore_databases_regex")))
    {
        int errcode;
        PCRE2_SIZE erroffset;
        pcre2_code* re = pcre2_compile((PCRE2_SPTR)param->value, PCRE2_ZERO_TERMINATED, 0,
                                       &errcode, &erroffset, NULL);

        if (re == NULL)
        {
            PCRE2_UCHAR errbuf[512];
            pcre2_get_error_message(errcode, errbuf, sizeof(errbuf));
            MXS_ERROR("Regex compilation failed at %d for regex '%s': %s",
                      (int)erroffset, param->value, errbuf);
            hashtable_free(router->ignored_dbs);
            MXS_FREE(router);
            return NULL;
        }

        pcre2_match_data* match_data = pcre2_match_data_create_from_pattern(re, NULL);

        if (match_data == NULL)
        {
            MXS_ERROR("PCRE2 match data creation failed. This"
                      " is most likely caused by a lack of available memory.");
            pcre2_code_free(re);
            hashtable_free(router->ignored_dbs);
            MXS_FREE(router);
            return NULL;
        }

        router->ignore_regex = re;
        router->ignore_match_data = match_data;
    }

    if ((param = config_get_param(conf, "ignore_databases")))
    {
        char val[strlen(param->value) + 1];
        strcpy(val, param->value);

        const char *sep = ", \t";
        char *sptr;
        char *tok = strtok_r(val, sep, &sptr);

        while (tok)
        {
            hashtable_add(router->ignored_dbs, tok, "");
            tok = strtok_r(NULL, sep, &sptr);
        }
    }

    bool failure = false;

    for (int i = 0; options && options[i]; i++)
    {
        char* value = strchr(options[i], '=');

        if (value == NULL)
        {
            MXS_ERROR("Unknown router options for %s", options[i]);
            failure = true;
            break;
        }

        *value = '\0';
        value++;

        if (strcmp(options[i], "max_sescmd_history") == 0)
        {
            router->schemarouter_config.max_sescmd_hist = atoi(value);
        }
        else if (strcmp(options[i], "disable_sescmd_history") == 0)
        {
            router->schemarouter_config.disable_sescmd_hist = config_truth_value(value);
        }
        else if (strcmp(options[i], "refresh_databases") == 0)
        {
            router->schemarouter_config.refresh_databases = config_truth_value(value);
        }
        else if (strcmp(options[i], "refresh_interval") == 0)
        {
            router->schemarouter_config.refresh_min_interval = atof(value);
        }
        else if (strcmp(options[i], "debug") == 0)
        {
            router->schemarouter_config.debug = config_truth_value(value);
        }
        else
        {
            MXS_ERROR("Unknown router options for %s", options[i]);
            failure = true;
            break;
        }
    }

    /** Setting a limit to the history size is not needed if it is disabled.*/
    if (router->schemarouter_config.disable_sescmd_hist && router->schemarouter_config.max_sescmd_hist > 0)
    {
        router->schemarouter_config.max_sescmd_hist = 0;
    }

    if (failure)
    {
        MXS_FREE(router);
        router = NULL;
    }

    return (MXS_ROUTER *)router;
}

/**
 * Check if the shard map is out of date and update its state if necessary.
 * @param router Router instance
 * @param map Shard map to update
 * @return Current state of the shard map
 */
enum shard_map_state shard_map_update_state(shard_map_t *self, ROUTER_INSTANCE* router)
{
    spinlock_acquire(&self->lock);
    double tdiff = difftime(time(NULL), self->last_updated);
    if (tdiff > router->schemarouter_config.refresh_min_interval)
    {
        self->state = SHMAP_STALE;
    }
    enum shard_map_state state = self->state;
    spinlock_release(&self->lock);
    return state;
}

/**
 * Associate a new session with this instance of the router.
 *
 * The session is used to store all the data required for a particular
 * client connection.
 *
 * @param instance      The router instance data
 * @param session       The session itself
 * @return Session specific data for this session
 */
static MXS_ROUTER_SESSION* newSession(MXS_ROUTER* router_inst, MXS_SESSION* session)
{
    backend_ref_t* backend_ref; /*< array of backend references (DCB, BACKEND, cursor) */
    ROUTER_CLIENT_SES* client_rses = NULL;
    ROUTER_INSTANCE* router      = (ROUTER_INSTANCE *)router_inst;
    bool succp;
    int router_nservers = 0; /*< # of servers in total */
    char db[MYSQL_DATABASE_MAXLEN + 1] = "";
    MySQLProtocol* protocol = session->client_dcb->protocol;
    MYSQL_session* data = session->client_dcb->data;
    bool using_db = false;
    bool have_db = false;

    /* To enable connecting directly to a sharded database we first need
     * to disable it for the client DCB's protocol so that we can connect to them*/
    if (protocol->client_capabilities & GW_MYSQL_CAPABILITIES_CONNECT_WITH_DB &&
        (have_db = strnlen(data->db, MYSQL_DATABASE_MAXLEN) > 0))
    {
        protocol->client_capabilities &= ~GW_MYSQL_CAPABILITIES_CONNECT_WITH_DB;
        strcpy(db, data->db);
        *data->db = 0;
        using_db = true;
        MXS_INFO("Client logging in directly to a database '%s', "
                 "postponing until databases have been mapped.", db);
    }

    if (!have_db)
    {
        MXS_INFO("Client'%s' connecting with empty database.", data->user);
    }

    client_rses = (ROUTER_CLIENT_SES *)MXS_CALLOC(1, sizeof(ROUTER_CLIENT_SES));

    if (client_rses == NULL)
    {
        return NULL;
    }
#if defined(SS_DEBUG)
    client_rses->rses_chk_top = CHK_NUM_ROUTER_SES;
    client_rses->rses_chk_tail = CHK_NUM_ROUTER_SES;
#endif

    client_rses->router = router;
    client_rses->rses_mysql_session = (MYSQL_session*)session->client_dcb->data;
    client_rses->rses_client_dcb = (DCB*)session->client_dcb;

    spinlock_acquire(&router->lock);

    shard_map_t *map = hashtable_fetch(router->shard_maps, session->client_dcb->user);
    enum shard_map_state state;

    if (map)
    {
        state = shard_map_update_state(map, router);
    }

    spinlock_release(&router->lock);

    if (map == NULL || state != SHMAP_READY)
    {
        if ((map = shard_map_alloc()) == NULL)
        {
            MXS_ERROR("Failed to allocate enough memory to create"
                      "new shard mapping. Session will be closed.");
            MXS_FREE(client_rses);
            return NULL;
        }
        client_rses->init = INIT_UNINT;
    }
    else
    {
        client_rses->init = INIT_READY;
        atomic_add(&router->stats.shmap_cache_hit, 1);
    }

    client_rses->shardmap = map;
    memcpy(&client_rses->rses_config, &router->schemarouter_config, sizeof(schemarouter_config_t));
    client_rses->n_sescmd = 0;
    client_rses->rses_config.last_refresh = time(NULL);

    if (using_db)
    {
        client_rses->init |= INIT_USE_DB;
    }
    /**
     * Set defaults to session variables.
     */
    client_rses->rses_autocommit_enabled = true;
    client_rses->rses_transaction_active = false;

    /**
     * Instead of calling this, ensure that there is at least one
     * responding server.
     */

    router_nservers = router->service->n_dbref;

    /**
     * Create backend reference objects for this session.
     */
    backend_ref = (backend_ref_t *)MXS_CALLOC(router_nservers, sizeof(backend_ref_t));

    if (backend_ref == NULL)
    {
        MXS_FREE(client_rses);
        return NULL;
    }
    /**
     * Initialize backend references with BACKEND ptr.
     * Initialize session command cursors for each backend reference.
     */

    int i = 0;

    for (SERVER_REF *ref = router->service->dbref; ref; ref = ref->next)
    {
        if (ref->active)
        {
#if defined(SS_DEBUG)
            backend_ref[i].bref_chk_top = CHK_NUM_BACKEND_REF;
            backend_ref[i].bref_chk_tail = CHK_NUM_BACKEND_REF;
            backend_ref[i].bref_sescmd_cur.scmd_cur_chk_top  = CHK_NUM_SESCMD_CUR;
            backend_ref[i].bref_sescmd_cur.scmd_cur_chk_tail = CHK_NUM_SESCMD_CUR;
#endif
            backend_ref[i].bref_state = 0;
            backend_ref[i].n_mapping_eof = 0;
            backend_ref[i].map_queue = NULL;
            backend_ref[i].bref_backend = ref;
            /** store pointers to sescmd list to both cursors */
            backend_ref[i].bref_sescmd_cur.scmd_cur_rses = client_rses;
            backend_ref[i].bref_sescmd_cur.scmd_cur_active = false;
            backend_ref[i].bref_sescmd_cur.scmd_cur_ptr_property =
                &client_rses->rses_properties[RSES_PROP_TYPE_SESCMD];
            backend_ref[i].bref_sescmd_cur.scmd_cur_cmd = NULL;
            i++;
        }
    }

    if (i < router_nservers)
    {
        router_nservers = i;
    }

    spinlock_init(&client_rses->rses_lock);
    client_rses->rses_backend_ref = backend_ref;
    client_rses->rses_nbackends = router_nservers;

    /**
     * Find a backend servers to connect to.
     * This command requires that rsession's lock is held.
     */
    if (!(succp = rses_begin_locked_router_action(client_rses)))
    {
        MXS_FREE(client_rses->rses_backend_ref);
        MXS_FREE(client_rses);
        return NULL;
    }
    /**
     * Connect to all backend servers
     */
    succp = connect_backend_servers(backend_ref, router_nservers, session, router);

    rses_end_locked_router_action(client_rses);

    if (!succp || !(succp = rses_begin_locked_router_action(client_rses)))
    {
        MXS_FREE(client_rses->rses_backend_ref);
        MXS_FREE(client_rses);
        return NULL;
    }

    if (db[0])
    {
        /* Store the database the client is connecting to */
        snprintf(client_rses->connect_db, MYSQL_DATABASE_MAXLEN + 1, "%s", db);
    }

    rses_end_locked_router_action(client_rses);

    atomic_add(&router->stats.sessions, 1);

    return (void *)client_rses;
}



/**
 * Close a session with the router, this is the mechanism
 * by which a router may cleanup data structure etc.
 *
 * @param instance      The router instance data
 * @param session       The session being closed
 */
static void closeSession(MXS_ROUTER* instance, MXS_ROUTER_SESSION* router_session)
{
    ROUTER_CLIENT_SES* router_cli_ses;
    ROUTER_INSTANCE* inst;
    backend_ref_t*     backend_ref;

    MXS_DEBUG("%lu [schemarouter:closeSession]", pthread_self());

    /**
     * router session can be NULL if newSession failed and it is discarding
     * its connections and DCB's.
     */
    if (router_session == NULL)
    {
        return;
    }
    router_cli_ses = (ROUTER_CLIENT_SES *)router_session;
    CHK_CLIENT_RSES(router_cli_ses);

    inst = router_cli_ses->router;
    backend_ref = router_cli_ses->rses_backend_ref;
    /**
     * Lock router client session for secure read and update.
     */
    if (!router_cli_ses->rses_closed && rses_begin_locked_router_action(router_cli_ses))
    {
        int i;
        /**
         * This sets router closed. Nobody is allowed to use router
         * whithout checking this first.
         */
        router_cli_ses->rses_closed = true;

        for (i = 0; i < router_cli_ses->rses_nbackends; i++)
        {
            backend_ref_t* bref = &backend_ref[i];
            DCB* dcb = bref->bref_dcb;
            /** Close those which had been connected */
            if (BREF_IS_IN_USE(bref))
            {
                CHK_DCB(dcb);
#if defined(SS_DEBUG)
                /**
                 * session must be moved to SESSION_STATE_STOPPING state before
                 * router session is closed.
                 */
                if (dcb->session != NULL)
                {
                    ss_dassert(dcb->session->state == SESSION_STATE_STOPPING);
                }
#endif
                /** Clean operation counter in bref and in SERVER */
                while (BREF_IS_WAITING_RESULT(bref))
                {
                    bref_clear_state(bref, BREF_WAITING_RESULT);
                }
                bref_clear_state(bref, BREF_IN_USE);
                bref_set_state(bref, BREF_CLOSED);
                /**
                 * closes protocol and dcb
                 */
                dcb_close(dcb);
                /** decrease server current connection counters */
                atomic_add(&bref->bref_backend->connections, -1);
            }
        }

        gwbuf_free(router_cli_ses->queue);

        /** Unlock */
        rses_end_locked_router_action(router_cli_ses);

        spinlock_acquire(&inst->lock);
        if (inst->stats.longest_sescmd < router_cli_ses->stats.longest_sescmd)
        {
            inst->stats.longest_sescmd = router_cli_ses->stats.longest_sescmd;
        }
        double ses_time = difftime(time(NULL), router_cli_ses->rses_client_dcb->session->stats.connect);
        if (inst->stats.ses_longest < ses_time)
        {
            inst->stats.ses_longest = ses_time;
        }
        if (inst->stats.ses_shortest > ses_time && inst->stats.ses_shortest > 0)
        {
            inst->stats.ses_shortest = ses_time;
        }

        inst->stats.ses_average =
            (ses_time + ((inst->stats.sessions - 1) * inst->stats.ses_average)) /
            (inst->stats.sessions);

        spinlock_release(&inst->lock);
    }
}

static void freeSession(MXS_ROUTER* router_instance, MXS_ROUTER_SESSION* router_client_session)
{
    ROUTER_CLIENT_SES* router_cli_ses = (ROUTER_CLIENT_SES *)router_client_session;

    for (int i = 0; i < router_cli_ses->rses_nbackends; i++)
    {
        gwbuf_free(router_cli_ses->rses_backend_ref[i].bref_pending_cmd);
    }

    /**
     * For each property type, walk through the list, finalize properties
     * and free the allocated memory.
     */
    for (int i = RSES_PROP_TYPE_FIRST; i < RSES_PROP_TYPE_COUNT; i++)
    {
        rses_property_t* p = router_cli_ses->rses_properties[i];
        rses_property_t* q = p;

        while (p != NULL)
        {
            q = p->rses_prop_next;
            rses_property_done(p);
            p = q;
        }
    }
    /*
     * We are no longer in the linked list, free
     * all the memory and other resources associated
     * to the client session.
     */
    MXS_FREE(router_cli_ses->rses_backend_ref);
    MXS_FREE(router_cli_ses);
    return;
}

/**
 * Provide the router with a pointer to a suitable backend dcb.
 *
 * Detect failures in server statuses and reselect backends if necessary
 * If name is specified, server name becomes primary selection criteria.
 * Similarly, if max replication lag is specified, skip backends which lag too
 * much.
 *
 * @param p_dcb Address of the pointer to the resulting DCB
 * @param rses  Pointer to router client session
 * @param btype Backend type
 * @param name  Name of the backend which is primarily searched. May be NULL.
 *
 * @return True if proper DCB was found, false otherwise.
 */
static bool get_shard_dcb(DCB**              p_dcb,
                          ROUTER_CLIENT_SES* rses,
                          char*              name)
{
    backend_ref_t* backend_ref;
    int i;
    bool succp = false;

    CHK_CLIENT_RSES(rses);
    ss_dassert(p_dcb != NULL && *(p_dcb) == NULL);

    if (p_dcb == NULL || name == NULL)
    {
        goto return_succp;
    }
    backend_ref = rses->rses_backend_ref;

    for (i = 0; i < rses->rses_nbackends; i++)
    {
        SERVER_REF* b = backend_ref[i].bref_backend;
        /**
         * To become chosen:
         * backend must be in use, name must match, and
         * the backend state must be RUNNING
         */
        if (BREF_IS_IN_USE((&backend_ref[i])) &&
            (strncasecmp(name, b->server->unique_name, PATH_MAX) == 0) &&
            SERVER_IS_RUNNING(b->server))
        {
            *p_dcb = backend_ref[i].bref_dcb;
            succp = true;
            ss_dassert(backend_ref[i].bref_dcb->state != DCB_STATE_ZOMBIE);
            goto return_succp;
        }
    }

return_succp:
    return succp;
}


/**
 * Examine the query type, transaction state and routing hints. Find out the
 * target for query routing.
 *
 *  @param qtype      Type of query
 *  @param trx_active Is transacation active or not
 *  @param hint       Pointer to list of hints attached to the query buffer
 *
 *  @return bitfield including the routing target, or the target server name
 *          if the query would otherwise be routed to slave.
 */
static route_target_t get_shard_route_target(qc_query_type_t qtype,
                                             bool            trx_active, /*< !!! turha ? */
                                             HINT*           hint) /*< !!! turha ? */
{
    route_target_t target = TARGET_UNDEFINED;

    /**
     * These queries are not affected by hints
     */
    if (qc_query_is_type(qtype, QUERY_TYPE_SESSION_WRITE) ||
        qc_query_is_type(qtype, QUERY_TYPE_PREPARE_STMT) ||
        qc_query_is_type(qtype, QUERY_TYPE_PREPARE_NAMED_STMT) ||
        qc_query_is_type(qtype, QUERY_TYPE_GSYSVAR_WRITE) ||
        /** enable or disable autocommit are always routed to all */
        qc_query_is_type(qtype, QUERY_TYPE_ENABLE_AUTOCOMMIT) ||
        qc_query_is_type(qtype, QUERY_TYPE_DISABLE_AUTOCOMMIT))
    {
        /** hints don't affect on routing */
        target = TARGET_ALL;
    }
    else if (qc_query_is_type(qtype, QUERY_TYPE_SYSVAR_READ) ||
             qc_query_is_type(qtype, QUERY_TYPE_GSYSVAR_READ))
    {
        target = TARGET_ANY;
    }
#if defined(SS_DEBUG)
    MXS_INFO("Selected target type \"%s\"", STRTARGET(target));
#endif
    return target;
}

/**
 * Check if the query is a DROP TABLE... query and
 * if it targets a temporary table, remove it from the hashtable.
 * @param instance Router instance
 * @param router_session Router client session
 * @param querybuf GWBUF containing the query
 * @param type The type of the query resolved so far
 */
void check_drop_tmp_table(MXS_ROUTER* instance,
                          void* router_session,
                          GWBUF* querybuf,
                          qc_query_type_t type)
{
    int tsize = 0, klen = 0, i;
    char** tbl = NULL;
    char *hkey, *dbname;

    ROUTER_CLIENT_SES* router_cli_ses = (ROUTER_CLIENT_SES *)router_session;
    rses_property_t* rses_prop_tmp;

    rses_prop_tmp = router_cli_ses->rses_properties[RSES_PROP_TYPE_TMPTABLES];
    dbname = router_cli_ses->current_db;

    if (qc_is_drop_table_query(querybuf))
    {
        tbl = qc_get_table_names(querybuf, &tsize, false);
        if (tbl != NULL)
        {
            for (i = 0; i < tsize; i++)
            {
                klen = strlen(dbname) + strlen(tbl[i]) + 2;
                hkey = MXS_CALLOC(klen, sizeof(char));
                MXS_ABORT_IF_NULL(hkey);
                strcpy(hkey, dbname);
                strcat(hkey, ".");
                strcat(hkey, tbl[i]);

                if (rses_prop_tmp && rses_prop_tmp->rses_prop_data.temp_tables)
                {
                    if (hashtable_delete(rses_prop_tmp->rses_prop_data.temp_tables,
                                         (void *)hkey))
                    {
                        MXS_INFO("Temporary table dropped: %s", hkey);
                    }
                }
                MXS_FREE(tbl[i]);
                MXS_FREE(hkey);
            }

            MXS_FREE(tbl);
        }
    }
}

/**
 * Check if the query targets a temporary table.
 * @param instance Router instance
 * @param router_session Router client session
 * @param querybuf GWBUF containing the query
 * @param type The type of the query resolved so far
 * @return The type of the query
 */
qc_query_type_t is_read_tmp_table(MXS_ROUTER* instance,
                                  void* router_session,
                                  GWBUF* querybuf,
                                  qc_query_type_t type)
{

    bool target_tmp_table = false;
    int tsize = 0, klen = 0, i;
    char** tbl = NULL;
    char *hkey, *dbname;

    ROUTER_CLIENT_SES* router_cli_ses = (ROUTER_CLIENT_SES *)router_session;
    qc_query_type_t qtype = type;
    rses_property_t* rses_prop_tmp;

    rses_prop_tmp = router_cli_ses->rses_properties[RSES_PROP_TYPE_TMPTABLES];
    dbname = router_cli_ses->current_db;

    if (qc_query_is_type(qtype, QUERY_TYPE_READ) ||
        qc_query_is_type(qtype, QUERY_TYPE_LOCAL_READ) ||
        qc_query_is_type(qtype, QUERY_TYPE_USERVAR_READ) ||
        qc_query_is_type(qtype, QUERY_TYPE_SYSVAR_READ) ||
        qc_query_is_type(qtype, QUERY_TYPE_GSYSVAR_READ))
    {
        tbl = qc_get_table_names(querybuf, &tsize, false);

        if (tbl != NULL && tsize > 0)
        {
            /** Query targets at least one table */
            for (i = 0; i < tsize && !target_tmp_table && tbl[i]; i++)
            {
                klen = strlen(dbname) + strlen(tbl[i]) + 2;
                hkey = MXS_CALLOC(klen, sizeof(char));
                MXS_ABORT_IF_NULL(hkey);
                strcpy(hkey, dbname);
                strcat(hkey, ".");
                strcat(hkey, tbl[i]);

                if (rses_prop_tmp && rses_prop_tmp->rses_prop_data.temp_tables)
                {

                    if ((target_tmp_table =
                             (bool)hashtable_fetch(rses_prop_tmp->rses_prop_data.temp_tables, (void *)hkey)))
                    {
                        /**Query target is a temporary table*/
                        qtype = QUERY_TYPE_READ_TMP_TABLE;
                        MXS_INFO("Query targets a temporary table: %s", hkey);
                    }
                }

                MXS_FREE(hkey);
            }
        }
    }

    if (tbl != NULL)
    {
        for (i = 0; i < tsize; i++)
        {
            MXS_FREE(tbl[i]);
        }
        MXS_FREE(tbl);
    }

    return qtype;
}

/**
 * If query is of type QUERY_TYPE_CREATE_TMP_TABLE then find out
 * the database and table name, create a hashvalue and
 * add it to the router client session's property. If property
 * doesn't exist then create it first.
 * @param instance Router instance
 * @param router_session Router client session
 * @param querybuf GWBUF containing the query
 * @param type The type of the query resolved so far
 */
void check_create_tmp_table(MXS_ROUTER* instance,
                            void* router_session,
                            GWBUF* querybuf,
                            qc_query_type_t type)
{
    int klen = 0;
    char *hkey, *dbname;

    ROUTER_CLIENT_SES* router_cli_ses = (ROUTER_CLIENT_SES *)router_session;
    rses_property_t* rses_prop_tmp;
    HASHTABLE* h;

    rses_prop_tmp = router_cli_ses->rses_properties[RSES_PROP_TYPE_TMPTABLES];
    dbname = router_cli_ses->current_db;

    if (qc_query_is_type(type, QUERY_TYPE_CREATE_TMP_TABLE))
    {
        bool  is_temp = true;
        char* tblname = NULL;

        tblname = qc_get_created_table_name(querybuf);

        if (tblname && strlen(tblname) > 0)
        {
            klen = strlen(dbname) + strlen(tblname) + 2;
            hkey = MXS_CALLOC(klen, sizeof(char));
            MXS_ABORT_IF_NULL(hkey);
            strcpy(hkey, dbname);
            strcat(hkey, ".");
            strcat(hkey, tblname);
        }
        else
        {
            hkey = NULL;
        }

        if (rses_prop_tmp == NULL)
        {
            if ((rses_prop_tmp =
                     (rses_property_t*)MXS_CALLOC(1, sizeof(rses_property_t))))
            {
#if defined(SS_DEBUG)
                rses_prop_tmp->rses_prop_chk_top = CHK_NUM_ROUTER_PROPERTY;
                rses_prop_tmp->rses_prop_chk_tail = CHK_NUM_ROUTER_PROPERTY;
#endif
                rses_prop_tmp->rses_prop_rsession = router_cli_ses;
                rses_prop_tmp->rses_prop_refcount = 1;
                rses_prop_tmp->rses_prop_next = NULL;
                rses_prop_tmp->rses_prop_type = RSES_PROP_TYPE_TMPTABLES;
                router_cli_ses->rses_properties[RSES_PROP_TYPE_TMPTABLES] = rses_prop_tmp;
            }
        }
        if (rses_prop_tmp)
        {
            if (rses_prop_tmp->rses_prop_data.temp_tables == NULL)
            {
                h = hashtable_alloc(SCHEMAROUTER_HASHSIZE, hashkeyfun, hashcmpfun);
                hashtable_memory_fns(h,
                                     hashtable_item_strdup, hashtable_item_strdup,
                                     hashtable_item_free, hashtable_item_free);
                if (h != NULL)
                {
                    rses_prop_tmp->rses_prop_data.temp_tables = h;
                }
                else
                {
                    MXS_ERROR("Failed to allocate a new hashtable.");
                }

            }

            if (hkey && rses_prop_tmp->rses_prop_data.temp_tables &&
                hashtable_add(rses_prop_tmp->rses_prop_data.temp_tables,
                              (void *)hkey,
                              (void *)is_temp) == 0) /*< Conflict in hash table */
            {
                MXS_INFO("Temporary table conflict in hashtable: %s", hkey);
            }
#if defined(SS_DEBUG)
            {
                bool retkey = hashtable_fetch(rses_prop_tmp->rses_prop_data.temp_tables, hkey);
                if (retkey)
                {
                    MXS_INFO("Temporary table added: %s", hkey);
                }
            }
#endif
        }

        MXS_FREE(hkey);
        MXS_FREE(tblname);
    }
}

int cmpfn(const void* a, const void *b)
{
    return strcmp(*(char* const *)a, *(char* const *)b);
}

/** Internal structure used to stream the list of databases */
struct string_array
{
    char** array;
    int position;
    int size;
};

/**
 * Callback for the database list streaming.
 * @param rset Result set which is being processed
 * @param data Pointer to struct string_array containing the database names
 * @return New resultset row or NULL if no more data is available. If memory allocation
 * failed, NULL is returned.
 */
RESULT_ROW *result_set_cb(struct resultset * rset, void *data)
{
    RESULT_ROW *row = NULL;
    struct string_array *strarray = (struct string_array*) data;

    if (strarray->position < strarray->size && (row = resultset_make_row(rset)))
    {
        if (resultset_row_set(row, 0, strarray->array[strarray->position++]) == 0)
        {
            resultset_free_row(row);
            row = NULL;
        }
    }

    return row;
}

/**
 * Generates a custom SHOW DATABASES result set from all the databases in the
 * hashtable. Only backend servers that are up and in a proper state are listed
 * in it.
 * @param router Router instance
 * @param client Router client session
 * @return True if the sending of the database list was successful, otherwise false
 */
bool send_database_list(ROUTER_INSTANCE* router, ROUTER_CLIENT_SES* client)
{
    bool rval = false;
    spinlock_acquire(&client->shardmap->lock);
    if (client->shardmap->state != SHMAP_UNINIT)
    {
        struct string_array strarray;
        const int size = hashtable_size(client->shardmap->hash);
        strarray.array = MXS_MALLOC(size * sizeof(char*));
        MXS_ABORT_IF_NULL(strarray.array);
        strarray.position = 0;
        HASHITERATOR *iter = hashtable_iterator(client->shardmap->hash);
        RESULTSET* resultset = resultset_create(result_set_cb, &strarray);

        if (strarray.array && iter && resultset)
        {
            char *key;
            int i = 0;
            while ((key = hashtable_next(iter)))
            {
                char *value = hashtable_fetch(client->shardmap->hash, key);
                SERVER * server = server_find_by_unique_name(value);
                if (SERVER_IS_RUNNING(server))
                {
                    strarray.array[i++] = key;
                }
            }
            strarray.size = i;
            qsort(strarray.array, strarray.size, sizeof(char*), cmpfn);
            if (resultset_add_column(resultset, "Database", MYSQL_DATABASE_MAXLEN,
                                     COL_TYPE_VARCHAR))
            {
                resultset_stream_mysql(resultset, client->rses_client_dcb);
                rval = true;
            }
        }
        resultset_free(resultset);
        hashtable_iterator_free(iter);
        MXS_FREE(strarray.array);
    }
    spinlock_release(&client->shardmap->lock);
    return rval;
}

/**
 * The main routing entry, this is called with every packet that is
 * received and has to be forwarded to the backend database.
 *
 * The routeQuery will make the routing decision based on the contents
 * of the instance, session and the query itself in the queue. The
 * data in the queue may not represent a complete query, it represents
 * the data that has been received. The query router itself is responsible
 * for buffering the partial query, a later call to the query router will
 * contain the remainder, or part thereof of the query.
 *
 * @param instance              The query router instance
 * @param router_session        The session associated with the client
 * @param querybuf              MaxScale buffer queue with received packet
 *
 * @return if succeed 1, otherwise 0
 * If routeQuery fails, it means that router session has failed.
 * In any tolerated failure, handleError is called and if necessary,
 * an error message is sent to the client.
 *
 */
static int routeQuery(MXS_ROUTER* instance,
                      MXS_ROUTER_SESSION* router_session,
                      GWBUF* qbuf)
{
    qc_query_type_t qtype = QUERY_TYPE_UNKNOWN;
    mysql_server_cmd_t packet_type;
    uint8_t* packet;
    int i, ret = 0;
    DCB* target_dcb = NULL;
    ROUTER_INSTANCE* inst = (ROUTER_INSTANCE *)instance;
    ROUTER_CLIENT_SES* router_cli_ses = (ROUTER_CLIENT_SES *)router_session;
    bool rses_is_closed = false;
    bool change_successful = false;
    route_target_t route_target = TARGET_UNDEFINED;
    bool succp = false;
    char* tname = NULL;
    char* targetserver = NULL;
    GWBUF* querybuf = qbuf;
    char db[MYSQL_DATABASE_MAXLEN + 1];
    char errbuf[26 + MYSQL_DATABASE_MAXLEN];
    CHK_CLIENT_RSES(router_cli_ses);

    ss_dassert(!GWBUF_IS_TYPE_UNDEFINED(querybuf));

    if (!rses_begin_locked_router_action(router_cli_ses))
    {
        MXS_INFO("Route query aborted! Routing session is closed <");
        ret = 0;
        goto retblock;
    }

    if (!(rses_is_closed = router_cli_ses->rses_closed))
    {
        if (router_cli_ses->init & INIT_UNINT)
        {
            /* Generate database list */
            gen_databaselist(inst, router_cli_ses);

        }

        /**
         * If the databases are still being mapped or if the client connected
         * with a default database but no database mapping was performed we need
         * to store the query. Once the databases have been mapped and/or the
         * default database is taken into use we can send the query forward.
         */
        if (router_cli_ses->init & (INIT_MAPPING | INIT_USE_DB))
        {
            int init_rval = 1;
            char* querystr = modutil_get_SQL(querybuf);
            MXS_INFO("Storing query for session %p: %s",
                     router_cli_ses->rses_client_dcb->session,
                     querystr);
            MXS_FREE(querystr);
            querybuf = gwbuf_make_contiguous(querybuf);
            GWBUF* ptr = router_cli_ses->queue;

            while (ptr && ptr->next)
            {
                ptr = ptr->next;
            }

            if (ptr == NULL)
            {
                router_cli_ses->queue = querybuf;
            }
            else
            {
                ptr->next = querybuf;

            }

            if (router_cli_ses->init  == (INIT_READY | INIT_USE_DB))
            {
                /**
                 * This state is possible if a client connects with a default database
                 * and the shard map was found from the router cache
                 */
                if (!handle_default_db(router_cli_ses))
                {
                    init_rval = 0;
                }
            }
            rses_end_locked_router_action(router_cli_ses);
            return init_rval;
        }

    }

    rses_end_locked_router_action(router_cli_ses);

    packet = GWBUF_DATA(querybuf);
    packet_type = packet[4];

    if (rses_is_closed)
    {
        /**
         * MYSQL_COM_QUIT may have sent by client and as a part of backend
         * closing procedure.
         */
        if (packet_type != MYSQL_COM_QUIT)
        {
            char* query_str = modutil_get_query(querybuf);

            MXS_ERROR("Can't route %s:%s:\"%s\" to "
                      "backend server. Router is closed.",
                      STRPACKETTYPE(packet_type),
                      STRQTYPE(qtype),
                      (query_str == NULL ? "(empty)" : query_str));
            MXS_FREE(query_str);
        }
        ret = 0;
        goto retblock;
    }

    /** If buffer is not contiguous, make it such */
    if (querybuf->next != NULL)
    {
        querybuf = gwbuf_make_contiguous(querybuf);
    }

    if (detect_show_shards(querybuf))
    {
        process_show_shards(router_cli_ses);
        ret = 1;
        goto retblock;
    }

    qc_query_op_t op = QUERY_OP_UNDEFINED;

    switch (packet_type)
    {
    case MYSQL_COM_QUIT:        /*< 1 QUIT will close all sessions */
    case MYSQL_COM_INIT_DB:     /*< 2 DDL must go to the master */
    case MYSQL_COM_REFRESH:     /*< 7 - I guess this is session but not sure */
    case MYSQL_COM_DEBUG:       /*< 0d all servers dump debug info to stdout */
    case MYSQL_COM_PING:        /*< 0e all servers are pinged */
    case MYSQL_COM_CHANGE_USER: /*< 11 all servers change it accordingly */
    case MYSQL_COM_STMT_CLOSE:  /*< free prepared statement */
    case MYSQL_COM_STMT_SEND_LONG_DATA: /*< send data to column */
    case MYSQL_COM_STMT_RESET:  /*< resets the data of a prepared statement */
        qtype = QUERY_TYPE_SESSION_WRITE;
        break;

    case MYSQL_COM_CREATE_DB:   /**< 5 DDL must go to the master */
    case MYSQL_COM_DROP_DB:     /**< 6 DDL must go to the master */
        qtype = QUERY_TYPE_WRITE;
        break;

    case MYSQL_COM_QUERY:
        qtype = qc_get_type_mask(querybuf);
        op = qc_get_operation(querybuf);
        break;

    case MYSQL_COM_STMT_PREPARE:
        qtype = qc_get_type_mask(querybuf);
        qtype |= QUERY_TYPE_PREPARE_STMT;
        break;

    case MYSQL_COM_STMT_EXECUTE:
        /** Parsing is not needed for this type of packet */
        qtype = QUERY_TYPE_EXEC_STMT;
        break;

    case MYSQL_COM_SHUTDOWN:       /**< 8 where should shutdown be routed ? */
    case MYSQL_COM_STATISTICS:     /**< 9 ? */
    case MYSQL_COM_PROCESS_INFO:   /**< 0a ? */
    case MYSQL_COM_CONNECT:        /**< 0b ? */
    case MYSQL_COM_PROCESS_KILL:   /**< 0c ? */
    case MYSQL_COM_TIME:           /**< 0f should this be run in gateway ? */
    case MYSQL_COM_DELAYED_INSERT: /**< 10 ? */
    case MYSQL_COM_DAEMON:         /**< 1d ? */
    default:
        break;
    } /**< switch by packet type */


    if (MXS_LOG_PRIORITY_IS_ENABLED(LOG_INFO))
    {
        uint8_t* packet = GWBUF_DATA(querybuf);
        unsigned char ptype = packet[4];
        size_t len = MXS_MIN(GWBUF_LENGTH(querybuf),
                             MYSQL_GET_PAYLOAD_LEN((unsigned char *)querybuf->start) - 1);
        char* data = (char*)&packet[5];
        char* contentstr = strndup(data, len);
        char* qtypestr = qc_typemask_to_string(qtype);

        MXS_INFO("> Cmd: %s, type: %s, "
                 "stmt: %s%s %s",
                 STRPACKETTYPE(ptype),
                 (qtypestr == NULL ? "N/A" : qtypestr),
                 contentstr,
                 (querybuf->hint == NULL ? "" : ", Hint:"),
                 (querybuf->hint == NULL ? "" : STRHINTTYPE(querybuf->hint->type)));

        MXS_FREE(contentstr);
        MXS_FREE(qtypestr);
    }
    /**
     * Find out whether the query should be routed to single server or to
     * all of them.
     */

    if (packet_type == MYSQL_COM_INIT_DB || op == QUERY_OP_CHANGE_DB)
    {
        spinlock_acquire(&router_cli_ses->shardmap->lock);
        change_successful = change_current_db(router_cli_ses->current_db,
                                              router_cli_ses->shardmap->hash,
                                              querybuf);
        spinlock_release(&router_cli_ses->shardmap->lock);
        if (!change_successful)
        {
            time_t now = time(NULL);
            if (router_cli_ses->rses_config.refresh_databases &&
                difftime(now, router_cli_ses->rses_config.last_refresh) >
                router_cli_ses->rses_config.refresh_min_interval)
            {
                spinlock_acquire(&router_cli_ses->shardmap->lock);
                router_cli_ses->shardmap->state = SHMAP_STALE;
                spinlock_release(&router_cli_ses->shardmap->lock);

                rses_begin_locked_router_action(router_cli_ses);

                router_cli_ses->rses_config.last_refresh = now;
                router_cli_ses->queue = querybuf;
                int rc_refresh = 1;

                if ((router_cli_ses->shardmap = shard_map_alloc()))
                {
                    gen_databaselist(inst, router_cli_ses);
                }
                else
                {
                    rc_refresh = 0;
                }
                rses_end_locked_router_action(router_cli_ses);
                return rc_refresh;
            }
            extract_database(querybuf, db);
            snprintf(errbuf, 25 + MYSQL_DATABASE_MAXLEN, "Unknown database: %s", db);
            if (router_cli_ses->rses_config.debug)
            {
                sprintf(errbuf + strlen(errbuf),
                        " ([%lu]: DB change failed)",
                        router_cli_ses->rses_client_dcb->session->ses_id);
            }

            write_error_to_client(router_cli_ses->rses_client_dcb,
                                  SCHEMA_ERR_DBNOTFOUND,
                                  SCHEMA_ERRSTR_DBNOTFOUND,
                                  errbuf);

            MXS_ERROR("Changing database failed.");
            ret = 1;
            goto retblock;
        }
    }

    /** Create the response to the SHOW DATABASES from the mapped databases */
    if (qc_query_is_type(qtype, QUERY_TYPE_SHOW_DATABASES))
    {
        if (send_database_list(inst, router_cli_ses))
        {
            ret = 1;
        }
        goto retblock;
    }

    route_target = get_shard_route_target(qtype,
                                          router_cli_ses->rses_transaction_active,
                                          querybuf->hint);

    if (packet_type == MYSQL_COM_INIT_DB || op == QUERY_OP_CHANGE_DB)
    {
        route_target = TARGET_UNDEFINED;

        spinlock_acquire(&router_cli_ses->shardmap->lock);
        tname = hashtable_fetch(router_cli_ses->shardmap->hash, router_cli_ses->current_db);


        if (tname)
        {
            MXS_INFO("INIT_DB for database '%s' on server '%s'",
                     router_cli_ses->current_db, tname);
            route_target = TARGET_NAMED_SERVER;
            targetserver = MXS_STRDUP_A(tname);
        }
        else
        {
            MXS_INFO("INIT_DB with unknown database");
        }
        spinlock_release(&router_cli_ses->shardmap->lock);
    }
    else if (route_target != TARGET_ALL)
    {
        /** If no database is found in the query and there is no active database
         * or hints in the query we need to route the query to the first available
         * server. This isn't ideal for monitoring server status but works if
         * we just want the server to send an error back. */

        spinlock_acquire(&router_cli_ses->shardmap->lock);
        if ((tname = get_shard_target_name(inst, router_cli_ses, querybuf, qtype)) != NULL)
        {
            bool shard_ok = check_shard_status(inst, tname);

            if (shard_ok)
            {
                route_target = TARGET_NAMED_SERVER;
                targetserver = MXS_STRDUP_A(tname);
            }
            else
            {
                MXS_INFO("Backend server '%s' is not in a viable state", tname);

                /**
                 * Shard is not a viable target right now so we check
                 * for an alternate backend with the database. If this is not found
                 * the target is undefined and an error will be returned to the client.
                 */
            }
        }
        spinlock_release(&router_cli_ses->shardmap->lock);
    }

    if (TARGET_IS_UNDEFINED(route_target))
    {
        spinlock_acquire(&router_cli_ses->shardmap->lock);
        tname = get_shard_target_name(inst, router_cli_ses, querybuf, qtype);

        if ((tname == NULL &&
             packet_type != MYSQL_COM_INIT_DB &&
             router_cli_ses->current_db[0] == '\0') ||
            packet_type == MYSQL_COM_FIELD_LIST ||
            (router_cli_ses->current_db[0] != '\0'))
        {
            /**
             * No current database and no databases in query or
             * the database is ignored, route to first available backend.
             */

            route_target = TARGET_ANY;
            MXS_INFO("Routing query to first available backend.");

        }
        else
        {
            if (tname)
            {
                targetserver = MXS_STRDUP_A(tname);
            }
            if (!change_successful)
            {
                /**
                 * Bad shard status. The changing of the database
                 * was not successful and the error message was already sent.
                 */

                ret = 1;
            }
            else
            {
                MXS_ERROR("Error : Router internal failure (schemarouter)");
                /** Something else went wrong, terminate connection */
                ret = 0;
            }
            spinlock_release(&router_cli_ses->shardmap->lock);
            goto retblock;
        }
        spinlock_release(&router_cli_ses->shardmap->lock);
    }

    if (TARGET_IS_ALL(route_target))
    {
        /**
         * It is not sure if the session command in question requires
         * response. Statement is examined in route_session_write.
         * Router locking is done inside the function.
         */
        succp = route_session_write(router_cli_ses,
                                    gwbuf_clone(querybuf),
                                    inst,
                                    packet_type,
                                    qtype);

        if (succp)
        {
            atomic_add(&inst->stats.n_sescmd, 1);
            atomic_add(&inst->stats.n_queries, 1);
            ret = 1;
        }
        goto retblock;
    }

    /** Lock router session */
    if (!rses_begin_locked_router_action(router_cli_ses))
    {
        MXS_INFO("Route query aborted! Routing session is closed <");
        ret = 0;
        goto retblock;
    }

    if (TARGET_IS_ANY(route_target))
    {
        for (int i = 0; i < router_cli_ses->rses_nbackends; i++)
        {
            SERVER *server = router_cli_ses->rses_backend_ref[i].bref_backend->server;
            if (SERVER_IS_RUNNING(server))
            {
                route_target = TARGET_NAMED_SERVER;
                targetserver = MXS_STRDUP_A(server->unique_name);
                break;
            }
        }

        if (TARGET_IS_ANY(route_target))
        {
            /**No valid backends alive*/
            MXS_ERROR("Failed to route query, no backends are available.");
            rses_end_locked_router_action(router_cli_ses);
            ret = 0;
            goto retblock;
        }

    }

    /**
     * Query is routed to one of the backends
     */
    if (TARGET_IS_NAMED_SERVER(route_target) && targetserver != NULL)
    {
        /**
         * Search backend server by name or replication lag.
         * If it fails, then try to find valid slave or master.
         */

        succp = get_shard_dcb(&target_dcb, router_cli_ses, targetserver);

        if (!succp)
        {
            MXS_INFO("Was supposed to route to named server "
                     "%s but couldn't find the server in a "
                     "suitable state.", targetserver);
        }

    }

    if (succp) /*< Have DCB of the target backend */
    {
        backend_ref_t*   bref;
        sescmd_cursor_t* scur;

        bref = get_bref_from_dcb(router_cli_ses, target_dcb);
        scur = &bref->bref_sescmd_cur;

        MXS_INFO("Route query to \t[%s]:%d <",
                 bref->bref_backend->server->name,
                 bref->bref_backend->server->port);
        /**
         * Store current stmt if execution of previous session command
         * haven't completed yet. Note that according to MySQL protocol
         * there can only be one such non-sescmd stmt at the time.
         */
        if (sescmd_cursor_is_active(scur))
        {
            ss_dassert((bref->bref_pending_cmd == NULL ||
                        router_cli_ses->rses_closed));
            bref->bref_pending_cmd = gwbuf_clone(querybuf);

            rses_end_locked_router_action(router_cli_ses);
            ret = 1;
            goto retblock;
        }

        if ((ret = target_dcb->func.write(target_dcb, gwbuf_clone(querybuf))) == 1)
        {
            backend_ref_t* bref;

            atomic_add(&inst->stats.n_queries, 1);

            /**
             * Add one query response waiter to backend reference
             */
            bref = get_bref_from_dcb(router_cli_ses, target_dcb);
            bref_set_state(bref, BREF_QUERY_ACTIVE);
            bref_set_state(bref, BREF_WAITING_RESULT);
        }
        else
        {
            MXS_ERROR("Routing query failed.");
        }
    }
    rses_end_locked_router_action(router_cli_ses);
retblock:
    MXS_FREE(targetserver);
    gwbuf_free(querybuf);

    return ret;
}

/**
 * @node Acquires lock to router client session if it is not closed.
 *
 * Parameters:
 * @param rses - in, use
 *
 *
 * @return true if router session was not closed. If return value is true
 * it means that router is locked, and must be unlocked later. False, if
 * router was closed before lock was acquired.
 *
 *
 * @details (write detailed description here)
 *
 */
static bool rses_begin_locked_router_action(
    ROUTER_CLIENT_SES* rses)
{
    bool succp = false;

    CHK_CLIENT_RSES(rses);

    if (rses->rses_closed)
    {
        goto return_succp;
    }
    spinlock_acquire(&rses->rses_lock);
    if (rses->rses_closed)
    {
        spinlock_release(&rses->rses_lock);
        goto return_succp;
    }
    succp = true;

return_succp:
    return succp;
}

/** to be inline'd */
/**
 * @node Releases router client session lock.
 *
 * Parameters:
 * @param rses - <usage>
 *          <description>
 *
 * @return void
 *
 *
 * @details (write detailed description here)
 *
 */
static void rses_end_locked_router_action(ROUTER_CLIENT_SES* rses)
{
    CHK_CLIENT_RSES(rses);
    spinlock_release(&rses->rses_lock);
}

/**
 * Diagnostics routine
 *
 * Print query router statistics to the DCB passed in
 *
 * @param       instance        The router instance
 * @param       dcb             The DCB for diagnostic output
 */
static void diagnostic(MXS_ROUTER *instance, DCB *dcb)
{
    ROUTER_INSTANCE *router = (ROUTER_INSTANCE *)instance;
    int i = 0;

    double sescmd_pct = router->stats.n_sescmd != 0 ?
                        100.0 * ((double)router->stats.n_sescmd / (double)router->stats.n_queries) :
                        0.0;

    /** Session command statistics */
    dcb_printf(dcb, "\n\33[1;4mSession Commands\33[0m\n");
    dcb_printf(dcb, "Total number of queries: %d\n",
               router->stats.n_queries);
    dcb_printf(dcb, "Percentage of session commands: %.2f\n",
               sescmd_pct);
    dcb_printf(dcb, "Longest chain of stored session commands: %d\n",
               router->stats.longest_sescmd);
    dcb_printf(dcb, "Session command history limit exceeded: %d times\n",
               router->stats.n_hist_exceeded);
    if (!router->schemarouter_config.disable_sescmd_hist)
    {
        dcb_printf(dcb, "Session command history: enabled\n");
        if (router->schemarouter_config.max_sescmd_hist == 0)
        {
            dcb_printf(dcb, "Session command history limit: unlimited\n");
        }
        else
        {
            dcb_printf(dcb, "Session command history limit: %d\n",
                       router->schemarouter_config.max_sescmd_hist);
        }
    }
    else
    {
        dcb_printf(dcb, "Session command history: disabled\n");
    }

    /** Session time statistics */

    if (router->stats.sessions > 0)
    {
        dcb_printf(dcb, "\n\33[1;4mSession Time Statistics\33[0m\n");
        dcb_printf(dcb, "Longest session: %.2lf seconds\n", router->stats.ses_longest);
        dcb_printf(dcb, "Shortest session: %.2lf seconds\n", router->stats.ses_shortest);
        dcb_printf(dcb, "Average session length: %.2lf seconds\n", router->stats.ses_average);
    }
    dcb_printf(dcb, "Shard map cache hits: %d\n", router->stats.shmap_cache_hit);
    dcb_printf(dcb, "Shard map cache misses: %d\n", router->stats.shmap_cache_miss);
    dcb_printf(dcb, "\n");
}

/**
 * Client Reply routine
 *
 * The routine will reply to client for session change with master server data
 *
 * @param       instance        The router instance
 * @param       router_session  The router session
 * @param       backend_dcb     The backend DCB
 * @param       queue           The GWBUF with reply data
 */
static void clientReply(MXS_ROUTER* instance,
                        MXS_ROUTER_SESSION* router_session,
                        GWBUF* buffer,
                        DCB* backend_dcb)
{
    DCB* client_dcb;
    ROUTER_CLIENT_SES* router_cli_ses;
    sescmd_cursor_t* scur = NULL;
    backend_ref_t* bref;
    GWBUF* writebuf = buffer;

    router_cli_ses = (ROUTER_CLIENT_SES *) router_session;
    CHK_CLIENT_RSES(router_cli_ses);

    /**
     * Lock router client session for secure read of router session members.
     * Note that this could be done without lock by using version #
     */
    if (!rses_begin_locked_router_action(router_cli_ses))
    {
        while ((writebuf = gwbuf_consume(writebuf, gwbuf_length(writebuf))));
        return;
    }
    /** Holding lock ensures that router session remains open */
    ss_dassert(backend_dcb->session != NULL);
    client_dcb = backend_dcb->session->client_dcb;

    /** Unlock */
    rses_end_locked_router_action(router_cli_ses);

    if (client_dcb == NULL || !rses_begin_locked_router_action(router_cli_ses))
    {
        while ((writebuf = gwbuf_consume(writebuf, gwbuf_length(writebuf))));
        return;
    }

    bref = get_bref_from_dcb(router_cli_ses, backend_dcb);

    if (bref == NULL)
    {
        /** Unlock router session */
        rses_end_locked_router_action(router_cli_ses);
        while ((writebuf = gwbuf_consume(writebuf, gwbuf_length(writebuf))))
        {
            ;
        }
        return;
    }

    MXS_DEBUG("Reply from [%s] session [%p]"
              " mapping [%s] queries queued [%s]",
              bref->bref_backend->server->unique_name,
              router_cli_ses->rses_client_dcb->session,
              router_cli_ses->init & INIT_MAPPING ? "true" : "false",
              router_cli_ses->queue == NULL ? "none" :
              router_cli_ses->queue->next ? "multiple" : "one");



    if (router_cli_ses->init & INIT_MAPPING)
    {
        int rc = inspect_backend_mapping_states(router_cli_ses, bref, &writebuf);

        while (writebuf && (writebuf = gwbuf_consume(writebuf, gwbuf_length(writebuf))))
        {
            ;
        }

        if (rc == 1)
        {
            spinlock_acquire(&router_cli_ses->shardmap->lock);

            router_cli_ses->shardmap->state = SHMAP_READY;
            router_cli_ses->shardmap->last_updated = time(NULL);
            spinlock_release(&router_cli_ses->shardmap->lock);

            rses_end_locked_router_action(router_cli_ses);

            synchronize_shard_map(router_cli_ses);

            if (!rses_begin_locked_router_action(router_cli_ses))
            {
                return;
            }

            /*
             * Check if the session is reconnecting with a database name
             * that is not in the hashtable. If the database is not found
             * then close the session.
             */
            router_cli_ses->init &= ~INIT_MAPPING;

            if (router_cli_ses->init & INIT_USE_DB)
            {
                bool success = handle_default_db(router_cli_ses);
                rses_end_locked_router_action(router_cli_ses);
                if (!success)
                {
                    dcb_close(router_cli_ses->rses_client_dcb);
                }
                return;
            }

            if (router_cli_ses->queue)
            {
                ss_dassert(router_cli_ses->init == INIT_READY);
                route_queued_query(router_cli_ses);
            }
            MXS_DEBUG("session [%p] database map finished.",
                      router_cli_ses);
        }

        rses_end_locked_router_action(router_cli_ses);

        if (rc == -1)
        {
            dcb_close(router_cli_ses->rses_client_dcb);
        }
        return;
    }

    if (router_cli_ses->init & INIT_USE_DB)
    {
        MXS_DEBUG("Reply to USE '%s' received for session %p",
                  router_cli_ses->connect_db,
                  router_cli_ses->rses_client_dcb->session);
        router_cli_ses->init &= ~INIT_USE_DB;
        strcpy(router_cli_ses->current_db, router_cli_ses->connect_db);
        ss_dassert(router_cli_ses->init == INIT_READY);

        if (router_cli_ses->queue)
        {
            route_queued_query(router_cli_ses);
        }

        rses_end_locked_router_action(router_cli_ses);
        if (writebuf)
        {
            while ((writebuf = gwbuf_consume(writebuf, gwbuf_length(writebuf))))
            {
                ;
            }
        }
        return;
    }

    if (router_cli_ses->queue)
    {
        ss_dassert(router_cli_ses->init == INIT_READY);
        route_queued_query(router_cli_ses);
        rses_end_locked_router_action(router_cli_ses);
        return;
    }

    CHK_BACKEND_REF(bref);
    scur = &bref->bref_sescmd_cur;
    /**
     * Active cursor means that reply is from session command
     * execution.
     */
    if (sescmd_cursor_is_active(scur))
    {
        if (MXS_LOG_PRIORITY_IS_ENABLED(LOG_ERR) &&
            MYSQL_IS_ERROR_PACKET(((uint8_t *) GWBUF_DATA(writebuf))))
        {
            uint8_t* buf = (uint8_t *) GWBUF_DATA((scur->scmd_cur_cmd->my_sescmd_buf));
            uint8_t* replybuf = (uint8_t *) GWBUF_DATA(writebuf);
            size_t len = MYSQL_GET_PAYLOAD_LEN(buf);
            size_t replylen = MYSQL_GET_PAYLOAD_LEN(replybuf);
            char* cmdstr = strndup(&((char *) buf)[5], len - 4);
            char* err = strndup(&((char *) replybuf)[8], 5);
            char* replystr = strndup(&((char *) replybuf)[13],
                                     replylen - 4 - 5);

            ss_dassert(len + 4 == GWBUF_LENGTH(scur->scmd_cur_cmd->my_sescmd_buf));

            MXS_ERROR("Failed to execute %s in [%s]:%d. %s %s",
                      cmdstr,
                      bref->bref_backend->server->name,
                      bref->bref_backend->server->port,
                      err,
                      replystr);

            MXS_FREE(cmdstr);
            MXS_FREE(err);
            MXS_FREE(replystr);
        }

        if (GWBUF_IS_TYPE_SESCMD_RESPONSE(writebuf))
        {
            /**
             * Discard all those responses that have already been sent to
             * the client. Return with buffer including response that
             * needs to be sent to client or NULL.
             */
            writebuf = sescmd_cursor_process_replies(writebuf, bref);
        }
        /**
         * If response will be sent to client, decrease waiter count.
         * This applies to session commands only. Counter decrement
         * for other type of queries is done outside this block.
         */
        if (writebuf != NULL && client_dcb != NULL)
        {
            /** Set response status as replied */
            bref_clear_state(bref, BREF_WAITING_RESULT);
        }
    }
    /**
     * Clear BREF_QUERY_ACTIVE flag and decrease waiter counter.
     * This applies for queries  other than session commands.
     */
    else if (BREF_IS_QUERY_ACTIVE(bref))
    {
        bref_clear_state(bref, BREF_QUERY_ACTIVE);
        /** Set response status as replied */
        bref_clear_state(bref, BREF_WAITING_RESULT);
    }

    if (writebuf != NULL && client_dcb != NULL)
    {
        unsigned char* cmd = (unsigned char*) writebuf->start;
        int state = router_cli_ses->init;
        /** Write reply to client DCB */
        MXS_INFO("returning reply [%s] "
                 "state [%s]  session [%p]",
                 PTR_IS_ERR(cmd) ? "ERR" : PTR_IS_OK(cmd) ? "OK" : "RSET",
                 state & INIT_UNINT ? "UNINIT" : state & INIT_MAPPING ? "MAPPING" : "READY",
                 router_cli_ses->rses_client_dcb->session);
        MXS_SESSION_ROUTE_REPLY(backend_dcb->session, writebuf);
    }
    /** Unlock router session */
    rses_end_locked_router_action(router_cli_ses);

    /** Lock router session */
    if (!rses_begin_locked_router_action(router_cli_ses))
    {
        /** Log to debug that router was closed */
        return;
    }
    /** There is one pending session command to be executed. */
    if (sescmd_cursor_is_active(scur))
    {

        MXS_INFO("Backend [%s]:%d processed reply and starts to execute "
                 "active cursor.",
                 bref->bref_backend->server->name,
                 bref->bref_backend->server->port);

        execute_sescmd_in_backend(bref);
    }
    else if (bref->bref_pending_cmd != NULL) /*< non-sescmd is waiting to be routed */
    {
        int ret;

        CHK_GWBUF(bref->bref_pending_cmd);

        if ((ret = bref->bref_dcb->func.write(bref->bref_dcb,
                                              gwbuf_clone(bref->bref_pending_cmd))) == 1)
        {
            ROUTER_INSTANCE* inst = (ROUTER_INSTANCE *) instance;
            atomic_add(&inst->stats.n_queries, 1);
            /**
             * Add one query response waiter to backend reference
             */
            bref_set_state(bref, BREF_QUERY_ACTIVE);
            bref_set_state(bref, BREF_WAITING_RESULT);
        }
        else
        {
            char* sql = modutil_get_SQL(bref->bref_pending_cmd);

            if (sql)
            {
                MXS_ERROR("Routing query \"%s\" failed.", sql);
                MXS_FREE(sql);
            }
            else
            {
                MXS_ERROR("Routing query failed.");
            }
        }
        gwbuf_free(bref->bref_pending_cmd);
        bref->bref_pending_cmd = NULL;
    }
    /** Unlock router session */
    rses_end_locked_router_action(router_cli_ses);

    return;
}

/** Compare number of connections from this router in backend servers */
int bref_cmp_router_conn(const void* bref1, const void* bref2)
{
    SERVER_REF* b1 = ((backend_ref_t *)bref1)->bref_backend;
    SERVER_REF* b2 = ((backend_ref_t *)bref2)->bref_backend;

    return ((1000 * b1->connections) / b1->weight)
           - ((1000 * b2->connections) / b2->weight);
}

/** Compare number of global connections in backend servers */
int bref_cmp_global_conn(const void* bref1, const void* bref2)
{
    SERVER_REF* b1 = ((backend_ref_t *)bref1)->bref_backend;
    SERVER_REF* b2 = ((backend_ref_t *)bref2)->bref_backend;

    return ((1000 * b1->server->stats.n_current) / b1->weight)
           - ((1000 * b2->server->stats.n_current) / b2->weight);
}


/** Compare replication lag between backend servers */
int bref_cmp_behind_master(const void* bref1, const void* bref2)
{
    SERVER_REF* b1 = ((backend_ref_t *)bref1)->bref_backend;
    SERVER_REF* b2 = ((backend_ref_t *)bref2)->bref_backend;

    return b1->server->rlag - b2->server->rlag;
}

/** Compare number of current operations in backend servers */
int bref_cmp_current_load(const void* bref1, const void* bref2)
{
    SERVER_REF* b1 = ((backend_ref_t *)bref1)->bref_backend;
    SERVER_REF* b2 = ((backend_ref_t *)bref2)->bref_backend;

    return ((1000 * b1->server->stats.n_current_ops) - b1->weight)
           - ((1000 * b2->server->stats.n_current_ops) - b2->weight);
}

static void bref_clear_state(backend_ref_t* bref, bref_state_t state)
{
    if (bref == NULL)
    {
        MXS_ERROR("[%s] Error: NULL parameter.", __FUNCTION__);
        return;
    }
    if (state != BREF_WAITING_RESULT)
    {
        bref->bref_state &= ~state;
    }
    else
    {
        int prev1;
        int prev2;

        /** Decrease waiter count */
        prev1 = atomic_add(&bref->bref_num_result_wait, -1);

        if (prev1 <= 0)
        {
            atomic_add(&bref->bref_num_result_wait, 1);
        }
        else
        {
            /** Decrease global operation count */
            prev2 = atomic_add(&bref->bref_backend->server->stats.n_current_ops, -1);
            ss_dassert(prev2 > 0);
            if (prev2 <= 0)
            {
                MXS_ERROR("[%s] Error: negative current operation count in backend %s:%u",
                          __FUNCTION__,
                          bref->bref_backend->server->name,
                          bref->bref_backend->server->port);
            }
        }
    }
}

static void bref_set_state(backend_ref_t* bref, bref_state_t state)
{
    if (bref == NULL)
    {
        MXS_ERROR("[%s] Error: NULL parameter.", __FUNCTION__);
        return;
    }
    if (state != BREF_WAITING_RESULT)
    {
        bref->bref_state |= state;
    }
    else
    {
        int prev1;
        int prev2;

        /** Increase waiter count */
        prev1 = atomic_add(&bref->bref_num_result_wait, 1);
        ss_dassert(prev1 >= 0);
        if (prev1 < 0)
        {
            MXS_ERROR("[%s] Error: negative number of connections waiting "
                      "for results in backend %s:%u",
                      __FUNCTION__,
                      bref->bref_backend->server->name,
                      bref->bref_backend->server->port);
        }
        /** Increase global operation count */
        prev2 = atomic_add(&bref->bref_backend->server->stats.n_current_ops, 1);
        ss_dassert(prev2 >= 0);
        if (prev2 < 0)
        {
            MXS_ERROR("[%s] Error: negative current operation count in backend %s:%u",
                      __FUNCTION__,
                      bref->bref_backend->server->name,
                      bref->bref_backend->server->port);
        }
    }
}

/**
 * @node Search all RUNNING backend servers and connect
 *
 * Parameters:
 * @param backend_ref - in, use, out
 *      Pointer to backend server reference object array.
 *      NULL is not allowed.
 *
 * @param router_nservers - in, use
 *      Number of backend server pointers pointed to by b.
 *
 * @param session - in, use
 *      MaxScale session pointer used when connection to backend is established.
 *
 * @param  router - in, use
 *      Pointer to router instance. Used when server states are qualified.
 *
 * @return true, if at least one master and one slave was found.
 *
 *
 * @details It is assumed that there is only one available server.
 *      There will be exactly as many backend references than there are
 *      connections because all servers are supposed to be operational. It is,
 *      however, possible that there are less available servers than expected.
 */
static bool connect_backend_servers(backend_ref_t*   backend_ref,
                                    int              router_nservers,
                                    MXS_SESSION*     session,
                                    ROUTER_INSTANCE* router)
{
    bool succp = true;
    /*
      bool is_synced_master;
      bool master_connected = true;
    */
    int servers_found = 0;
    int servers_connected = 0;
    int slaves_connected = 0;
    int i;
    /*
      select_criteria_t select_criteria = LEAST_GLOBAL_CONNECTIONS;
    */

#if defined(EXTRA_SS_DEBUG)
    MXS_INFO("Servers and conns before ordering:");

    for (i = 0; i < router_nservers; i++)
    {
        BACKEND* b = backend_ref[i].bref_backend;

        MXS_INFO("bref %p %d %s %d:%d",
                 &backend_ref[i],
                 backend_ref[i].bref_state,
                 b->backend_server->name,
                 b->backend_server->port,
                 b->backend_conn_count);
    }
#endif

    if (MXS_LOG_PRIORITY_IS_ENABLED(LOG_INFO))
    {
        MXS_INFO("Servers and connection counts:");

        for (i = 0; i < router_nservers; i++)
        {
            SERVER_REF* b = backend_ref[i].bref_backend;

            MXS_INFO("MaxScale connections : %d (%d) in \t[%s]:%d %s",
                     b->connections,
                     b->server->stats.n_current,
                     b->server->name,
                     b->server->port,
                     STRSRVSTATUS(b->server));
        }
    } /*< log only */
    /**
     * Scan server list and connect each of them. None should fail or session
     * can't be established.
     */
    for (i = 0; i < router_nservers; i++)
    {
        SERVER_REF* b = backend_ref[i].bref_backend;

        if (SERVER_IS_RUNNING(b->server))
        {
            servers_found += 1;

            /** Server is already connected */
            if (BREF_IS_IN_USE((&backend_ref[i])))
            {
                slaves_connected += 1;
            }
            /** New server connection */
            else
            {
                backend_ref[i].bref_dcb = dcb_connect(b->server,
                                                      session,
                                                      b->server->protocol);

                if (backend_ref[i].bref_dcb != NULL)
                {
                    servers_connected += 1;
                    /**
                     * Start executing session command
                     * history.
                     */
                    execute_sescmd_history(&backend_ref[i]);
                    /**
                     * When server fails, this callback
                     * is called.
                     * !!! Todo, routine which removes
                     * corresponding entries from the hash
                     * table.
                     */

                    backend_ref[i].bref_state = 0;
                    bref_set_state(&backend_ref[i], BREF_IN_USE);
                    /**
                     * Increase backend connection counter.
                     * Server's stats are _increased_ in
                     * dcb.c:dcb_alloc !
                     * But decreased in the calling function
                     * of dcb_close.
                     */
                    atomic_add(&b->connections, 1);

                    dcb_add_callback(backend_ref[i].bref_dcb,
                                     DCB_REASON_NOT_RESPONDING,
                                     &router_handle_state_switch,
                                     (void *)&backend_ref[i]);
                }
                else
                {
                    succp = false;
                    MXS_ERROR("Unable to establish "
                              "connection with slave [%s]:%d",
                              b->server->name,
                              b->server->port);
                    /* handle connect error */
                    break;
                }
            }
        }
    } /*< for */

#if defined(EXTRA_SS_DEBUG)
    MXS_INFO("Servers and conns after ordering:");

    for (i = 0; i < router_nservers; i++)
    {
        BACKEND* b = backend_ref[i].bref_backend;

        MXS_INFO("bref %p %d %s %d:%d",
                 &backend_ref[i],
                 backend_ref[i].bref_state,
                 b->backend_server->name,
                 b->backend_server->port,
                 b->backend_conn_count);
    }
#endif
    /**
     * Successful cases
     */
    if (servers_connected == router_nservers)
    {
        succp = true;

        if (MXS_LOG_PRIORITY_IS_ENABLED(LOG_INFO))
        {
            for (i = 0; i < router_nservers; i++)
            {
                SERVER_REF* b = backend_ref[i].bref_backend;

                if (BREF_IS_IN_USE((&backend_ref[i])))
                {
                    MXS_INFO("Connected %s in \t[%s]:%d",
                             STRSRVSTATUS(b->server),
                             b->server->name,
                             b->server->port);
                }
            } /* for */
        }
    }

    return succp;
}

/**
 * Create a generic router session property strcture.
 */
static rses_property_t* rses_property_init(rses_property_type_t prop_type)
{
    rses_property_t* prop;

    prop = (rses_property_t*)MXS_CALLOC(1, sizeof(rses_property_t));
    if (prop == NULL)
    {
        goto return_prop;
    }
    prop->rses_prop_type = prop_type;
#if defined(SS_DEBUG)
    prop->rses_prop_chk_top = CHK_NUM_ROUTER_PROPERTY;
    prop->rses_prop_chk_tail = CHK_NUM_ROUTER_PROPERTY;
#endif

return_prop:
    CHK_RSES_PROP(prop);
    return prop;
}

/**
 * Property is freed at the end of router client session.
 */
static void rses_property_done(rses_property_t* prop)
{
    CHK_RSES_PROP(prop);

    switch (prop->rses_prop_type)
    {
    case RSES_PROP_TYPE_SESCMD:
        mysql_sescmd_done(&prop->rses_prop_data.sescmd);
        break;

    case RSES_PROP_TYPE_TMPTABLES:
        hashtable_free(prop->rses_prop_data.temp_tables);
        break;

    default:
        MXS_DEBUG("%lu [rses_property_done] Unknown property type %d "
                  "in property %p",
                  pthread_self(),
                  prop->rses_prop_type,
                  prop);
        ss_dassert(false);
        break;
    }
    MXS_FREE(prop);
}

/**
 * Add property to the router_client_ses structure's rses_properties
 * array. The slot is determined by the type of property.
 * In each slot there is a list of properties of similar type.
 *
 * Router client session must be locked.
 */
static void rses_property_add(ROUTER_CLIENT_SES* rses,
                              rses_property_t*   prop)
{
    rses_property_t* p;

    CHK_CLIENT_RSES(rses);
    CHK_RSES_PROP(prop);
    ss_dassert(SPINLOCK_IS_LOCKED(&rses->rses_lock));

    prop->rses_prop_rsession = rses;
    p = rses->rses_properties[prop->rses_prop_type];

    if (p == NULL)
    {
        rses->rses_properties[prop->rses_prop_type] = prop;
    }
    else
    {
        while (p->rses_prop_next != NULL)
        {
            p = p->rses_prop_next;
        }
        p->rses_prop_next = prop;
    }
}

/**
 * Router session must be locked.
 * Return session command pointer if succeed, NULL if failed.
 */
static mysql_sescmd_t* rses_property_get_sescmd(rses_property_t* prop)
{
    mysql_sescmd_t* sescmd;

    CHK_RSES_PROP(prop);
    ss_dassert(prop->rses_prop_rsession == NULL ||
               SPINLOCK_IS_LOCKED(&prop->rses_prop_rsession->rses_lock));

    sescmd = &prop->rses_prop_data.sescmd;

    if (sescmd != NULL)
    {
        CHK_MYSQL_SESCMD(sescmd);
    }
    return sescmd;
}

/**
 * Create session command property.
 */
static mysql_sescmd_t* mysql_sescmd_init(rses_property_t*   rses_prop,
                                         GWBUF*             sescmd_buf,
                                         unsigned char      packet_type,
                                         ROUTER_CLIENT_SES* rses)
{
    mysql_sescmd_t* sescmd;

    CHK_RSES_PROP(rses_prop);
    /** Can't call rses_property_get_sescmd with uninitialized sescmd */
    sescmd = &rses_prop->rses_prop_data.sescmd;
    sescmd->my_sescmd_prop = rses_prop; /*< reference to owning property */
#if defined(SS_DEBUG)
    sescmd->my_sescmd_chk_top  = CHK_NUM_MY_SESCMD;
    sescmd->my_sescmd_chk_tail = CHK_NUM_MY_SESCMD;
#endif
    /** Set session command buffer */
    sescmd->my_sescmd_buf  = sescmd_buf;
    sescmd->my_sescmd_packet_type = packet_type;
    sescmd->position = atomic_add(&rses->pos_generator, 1);
    return sescmd;
}

static void mysql_sescmd_done(mysql_sescmd_t* sescmd)
{
    CHK_RSES_PROP(sescmd->my_sescmd_prop);
    gwbuf_free(sescmd->my_sescmd_buf);
    memset(sescmd, 0, sizeof(mysql_sescmd_t));
}

/**
 * All cases where backend message starts at least with one response to session
 * command are handled here.
 * Read session commands from property list. If command is already replied,
 * discard packet. Else send reply to client. In both cases move cursor forward
 * until all session command replies are handled.
 *
 * Cases that are expected to happen and which are handled:
 * s = response not yet replied to client, S = already replied response,
 * q = query
 * 1. q+        for example : select * from mysql.user
 * 2. s+        for example : set autocommit=1
 * 3. S+
 * 4. sq+
 * 5. Sq+
 * 6. Ss+
 * 7. Ss+q+
 * 8. S+q+
 * 9. s+q+
 */
static GWBUF* sescmd_cursor_process_replies(GWBUF*         replybuf,
                                            backend_ref_t* bref)
{
    mysql_sescmd_t* scmd;
    sescmd_cursor_t* scur;

    scur = &bref->bref_sescmd_cur;
    ss_dassert(SPINLOCK_IS_LOCKED(&(scur->scmd_cur_rses->rses_lock)));
    scmd = sescmd_cursor_get_command(scur);

    CHK_GWBUF(replybuf);

    /**
     * Walk through packets in the message and the list of session
     * commands.
     */
    while (scmd != NULL && replybuf != NULL)
    {
        scur->position = scmd->position;
        /** Faster backend has already responded to client : discard */
        if (scmd->my_sescmd_is_replied)
        {
            bool last_packet = false;

            CHK_GWBUF(replybuf);

            while (!last_packet)
            {
                int  buflen;

                buflen = GWBUF_LENGTH(replybuf);
                last_packet = GWBUF_IS_TYPE_RESPONSE_END(replybuf);
                /** discard packet */
                replybuf = gwbuf_consume(replybuf, buflen);
            }
            /** Set response status received */
            bref_clear_state(bref, BREF_WAITING_RESULT);
        }
        /** Response is in the buffer and it will be sent to client. */
        else if (replybuf != NULL)
        {
            /** Mark the rest session commands as replied */
            scmd->my_sescmd_is_replied = true;
        }

        if (sescmd_cursor_next(scur))
        {
            scmd = sescmd_cursor_get_command(scur);
        }
        else
        {
            scmd = NULL;
            /** All session commands are replied */
            scur->scmd_cur_active = false;
        }
    }
    ss_dassert(replybuf == NULL || *scur->scmd_cur_ptr_property == NULL);

    return replybuf;
}



/**
 * Get the address of current session command.
 *
 * Router session must be locked */
static mysql_sescmd_t* sescmd_cursor_get_command(sescmd_cursor_t* scur)
{
    mysql_sescmd_t* scmd;

    ss_dassert(SPINLOCK_IS_LOCKED(&(scur->scmd_cur_rses->rses_lock)));
    scur->scmd_cur_cmd = rses_property_get_sescmd(*scur->scmd_cur_ptr_property);

    CHK_MYSQL_SESCMD(scur->scmd_cur_cmd);

    scmd = scur->scmd_cur_cmd;

    return scmd;
}

/** router must be locked */
static bool sescmd_cursor_is_active(sescmd_cursor_t* sescmd_cursor)
{
    bool succp;
    ss_dassert(SPINLOCK_IS_LOCKED(&sescmd_cursor->scmd_cur_rses->rses_lock));

    succp = sescmd_cursor->scmd_cur_active;
    return succp;
}

/** router must be locked */
static void sescmd_cursor_set_active(sescmd_cursor_t* sescmd_cursor,
                                     bool             value)
{
    ss_dassert(SPINLOCK_IS_LOCKED(&sescmd_cursor->scmd_cur_rses->rses_lock));
    /** avoid calling unnecessarily */
    ss_dassert(sescmd_cursor->scmd_cur_active != value);
    sescmd_cursor->scmd_cur_active = value;
}

/**
 * Clone session command's command buffer.
 * Router session must be locked
 */
static GWBUF* sescmd_cursor_clone_querybuf(sescmd_cursor_t* scur)
{
    GWBUF* buf;
    ss_dassert(scur->scmd_cur_cmd != NULL);

    buf = gwbuf_clone(scur->scmd_cur_cmd->my_sescmd_buf);

    CHK_GWBUF(buf);
    return buf;
}

static bool sescmd_cursor_history_empty(sescmd_cursor_t* scur)
{
    bool succp;

    CHK_SESCMD_CUR(scur);

    if (scur->scmd_cur_rses->rses_properties[RSES_PROP_TYPE_SESCMD] == NULL)
    {
        succp = true;
    }
    else
    {
        succp = false;
    }

    return succp;
}

static void sescmd_cursor_reset(sescmd_cursor_t* scur)
{
    ROUTER_CLIENT_SES* rses;
    CHK_SESCMD_CUR(scur);
    CHK_CLIENT_RSES(scur->scmd_cur_rses);
    rses = scur->scmd_cur_rses;

    scur->scmd_cur_ptr_property = &rses->rses_properties[RSES_PROP_TYPE_SESCMD];

    CHK_RSES_PROP((*scur->scmd_cur_ptr_property));
    scur->scmd_cur_active = false;
    scur->scmd_cur_cmd = &(*scur->scmd_cur_ptr_property)->rses_prop_data.sescmd;
}

static bool execute_sescmd_history(backend_ref_t* bref)
{
    bool             succp;
    sescmd_cursor_t* scur;
    CHK_BACKEND_REF(bref);

    scur = &bref->bref_sescmd_cur;
    CHK_SESCMD_CUR(scur);

    if (!sescmd_cursor_history_empty(scur))
    {
        sescmd_cursor_reset(scur);
        succp = execute_sescmd_in_backend(bref);
    }
    else
    {
        succp = true;
    }

    return succp;
}

/**
 * If session command cursor is passive, sends the command to backend for
 * execution.
 *
 * Returns true if command was sent or added successfully to the queue.
 * Returns false if command sending failed or if there are no pending session
 *      commands.
 *
 * Router session must be locked.
 */
static bool execute_sescmd_in_backend(backend_ref_t* backend_ref)
{
    DCB* dcb;
    bool succp;
    int rc = 0;
    sescmd_cursor_t* scur;

    if (BREF_IS_CLOSED(backend_ref))
    {
        succp = false;
        goto return_succp;
    }
    dcb = backend_ref->bref_dcb;

    CHK_DCB(dcb);
    CHK_BACKEND_REF(backend_ref);

    /**
     * Get cursor pointer and copy of command buffer to cursor.
     */
    scur = &backend_ref->bref_sescmd_cur;

    /** Return if there are no pending ses commands */
    if (sescmd_cursor_get_command(scur) == NULL)
    {
        succp = false;
        MXS_INFO("Cursor had no pending session commands.");

        goto return_succp;
    }

    if (!sescmd_cursor_is_active(scur))
    {
        /** Cursor is left active when function returns. */
        sescmd_cursor_set_active(scur, true);
    }

    switch (scur->scmd_cur_cmd->my_sescmd_packet_type)
    {
    case MYSQL_COM_CHANGE_USER:
        /** This makes it possible to handle replies correctly */
        gwbuf_set_type(scur->scmd_cur_cmd->my_sescmd_buf, GWBUF_TYPE_SESCMD);
        rc = dcb->func.auth(dcb,
                            NULL,
                            dcb->session,
                            sescmd_cursor_clone_querybuf(scur));
        break;

    case MYSQL_COM_QUERY:
    default:
        /**
         * Mark session command buffer, it triggers writing
         * MySQL command to protocol
         */
        gwbuf_set_type(scur->scmd_cur_cmd->my_sescmd_buf, GWBUF_TYPE_SESCMD);
        rc = dcb->func.write(dcb, sescmd_cursor_clone_querybuf(scur));
        break;
    }

    if (rc == 1)
    {
        succp = true;
    }
    else
    {
        succp = false;
    }
return_succp:
    return succp;
}


/**
 * Moves cursor to next property and copied address of its sescmd to cursor.
 * Current propery must be non-null.
 * If current property is the last on the list, *scur->scmd_ptr_property == NULL
 *
 * Router session must be locked
 */
static bool sescmd_cursor_next(sescmd_cursor_t* scur)
{
    bool succp = false;
    rses_property_t* prop_curr;
    rses_property_t* prop_next;

    ss_dassert(scur != NULL);
    ss_dassert(*(scur->scmd_cur_ptr_property) != NULL);
    ss_dassert(SPINLOCK_IS_LOCKED(&(*(scur->scmd_cur_ptr_property))->rses_prop_rsession->rses_lock));

    /** Illegal situation */
    if (scur == NULL ||
        *scur->scmd_cur_ptr_property == NULL ||
        scur->scmd_cur_cmd == NULL)
    {
        /** Log error */
        goto return_succp;
    }
    prop_curr = *(scur->scmd_cur_ptr_property);

    CHK_MYSQL_SESCMD(scur->scmd_cur_cmd);
    ss_dassert(prop_curr == mysql_sescmd_get_property(scur->scmd_cur_cmd));
    CHK_RSES_PROP(prop_curr);

    /** Copy address of pointer to next property */
    scur->scmd_cur_ptr_property = &(prop_curr->rses_prop_next);
    prop_next = *scur->scmd_cur_ptr_property;
    ss_dassert(prop_next == *(scur->scmd_cur_ptr_property));

    /** If there is a next property move forward */
    if (prop_next != NULL)
    {
        CHK_RSES_PROP(prop_next);
        CHK_RSES_PROP((*(scur->scmd_cur_ptr_property)));

        /** Get pointer to next property's sescmd */
        scur->scmd_cur_cmd = rses_property_get_sescmd(prop_next);

        ss_dassert(prop_next == scur->scmd_cur_cmd->my_sescmd_prop);
        CHK_MYSQL_SESCMD(scur->scmd_cur_cmd);
        CHK_RSES_PROP(scur->scmd_cur_cmd->my_sescmd_prop);
    }
    else
    {
        /** No more properties, can't proceed. */
        goto return_succp;
    }

    if (scur->scmd_cur_cmd != NULL)
    {
        succp = true;
    }
    else
    {
        ss_dassert(false); /*< Log error, sescmd shouldn't be NULL */
    }
return_succp:
    return succp;
}

static rses_property_t* mysql_sescmd_get_property(mysql_sescmd_t* scmd)
{
    CHK_MYSQL_SESCMD(scmd);
    return scmd->my_sescmd_prop;
}

/**
 * @brief Get router capabilities
 *
 * @return Always  RCAP_TYPE_CONTIGUOUS_INPUT
 */
static uint64_t getCapabilities(MXS_ROUTER* instance)
{
    return RCAP_TYPE_CONTIGUOUS_INPUT;
}

/**
 * Execute in backends used by current router session.
 * Save session variable commands to router session property
 * struct. Thus, they can be replayed in backends which are
 * started and joined later.
 *
 * Suppress redundant OK packets sent by backends.
 *
 * The first OK packet is replied to the client.
 * Return true if succeed, false is returned if router session was closed or
 * if execute_sescmd_in_backend failed.
 */
static bool route_session_write(ROUTER_CLIENT_SES* router_cli_ses,
                                GWBUF*             querybuf,
                                ROUTER_INSTANCE*   inst,
                                unsigned char      packet_type,
                                qc_query_type_t    qtype)
{
    bool succp = false;
    rses_property_t* prop;
    backend_ref_t* backend_ref;
    int i;

    MXS_INFO("Session write, routing to all servers.");

    backend_ref = router_cli_ses->rses_backend_ref;

    /**
     * These are one-way messages and server doesn't respond to them.
     * Therefore reply processing is unnecessary and session
     * command property is not needed. It is just routed to all available
     * backends.
     */
    if (packet_type == MYSQL_COM_STMT_SEND_LONG_DATA ||
        packet_type == MYSQL_COM_QUIT ||
        packet_type == MYSQL_COM_STMT_CLOSE)
    {
        int rc;

        succp = true;

        /** Lock router session */
        if (!rses_begin_locked_router_action(router_cli_ses))
        {
            succp = false;
            goto return_succp;
        }

        for (i = 0; i < router_cli_ses->rses_nbackends; i++)
        {
            DCB* dcb = backend_ref[i].bref_dcb;

            if (MXS_LOG_PRIORITY_IS_ENABLED(LOG_INFO))
            {
                MXS_INFO("Route query to %s\t[%s]:%d%s",
                         (SERVER_IS_MASTER(backend_ref[i].bref_backend->server) ?
                          "master" : "slave"),
                         backend_ref[i].bref_backend->server->name,
                         backend_ref[i].bref_backend->server->port,
                         (i + 1 == router_cli_ses->rses_nbackends ? " <" : ""));
            }

            if (BREF_IS_IN_USE((&backend_ref[i])))
            {
                rc = dcb->func.write(dcb, gwbuf_clone(querybuf));
                if (rc != 1)
                {
                    succp = false;
                }
            }
        }
        rses_end_locked_router_action(router_cli_ses);
        gwbuf_free(querybuf);
        goto return_succp;
    }
    /** Lock router session */
    if (!rses_begin_locked_router_action(router_cli_ses))
    {
        succp = false;
        goto return_succp;
    }

    if (router_cli_ses->rses_nbackends <= 0)
    {
        succp = false;
        goto return_succp;
    }

    if (router_cli_ses->rses_config.max_sescmd_hist > 0 &&
        router_cli_ses->n_sescmd >= router_cli_ses->rses_config.max_sescmd_hist)
    {
        MXS_ERROR("Router session exceeded session command history limit of %d. "
                  "Closing router session.",
                  router_cli_ses->rses_config.max_sescmd_hist);
        gwbuf_free(querybuf);
        atomic_add(&router_cli_ses->router->stats.n_hist_exceeded, 1);
        rses_end_locked_router_action(router_cli_ses);
        router_cli_ses->rses_client_dcb->func.hangup(router_cli_ses->rses_client_dcb);

        goto return_succp;
    }

    if (router_cli_ses->rses_config.disable_sescmd_hist)
    {
        rses_property_t *prop, *tmp;
        backend_ref_t* bref;
        bool conflict;

        prop = router_cli_ses->rses_properties[RSES_PROP_TYPE_SESCMD];
        while (prop)
        {
            conflict = false;

            for (i = 0; i < router_cli_ses->rses_nbackends; i++)
            {
                bref = &backend_ref[i];
                if (BREF_IS_IN_USE(bref))
                {

                    if (bref->bref_sescmd_cur.position <= prop->rses_prop_data.sescmd.position)
                    {
                        conflict = true;
                        break;
                    }
                }
            }

            if (conflict)
            {
                break;
            }

            tmp = prop;
            router_cli_ses->rses_properties[RSES_PROP_TYPE_SESCMD] = prop->rses_prop_next;
            rses_property_done(tmp);
            prop = router_cli_ses->rses_properties[RSES_PROP_TYPE_SESCMD];
        }
    }

    /**
     *
     * Additional reference is created to querybuf to
     * prevent it from being released before properties
     * are cleaned up as a part of router session clean-up.
     */
    prop = rses_property_init(RSES_PROP_TYPE_SESCMD);
    mysql_sescmd_init(prop, querybuf, packet_type, router_cli_ses);

    /** Add sescmd property to router client session */
    rses_property_add(router_cli_ses, prop);
    atomic_add(&router_cli_ses->stats.longest_sescmd, 1);
    atomic_add(&router_cli_ses->n_sescmd, 1);

    for (i = 0; i < router_cli_ses->rses_nbackends; i++)
    {
        if (BREF_IS_IN_USE((&backend_ref[i])))
        {
            sescmd_cursor_t* scur;

            if (MXS_LOG_PRIORITY_IS_ENABLED(LOG_INFO))
            {
                MXS_INFO("Route query to %s\t[%s]:%d%s",
                         (SERVER_IS_MASTER(backend_ref[i].bref_backend->server) ?
                          "master" : "slave"),
                         backend_ref[i].bref_backend->server->name,
                         backend_ref[i].bref_backend->server->port,
                         (i + 1 == router_cli_ses->rses_nbackends ? " <" : ""));
            }

            scur = backend_ref_get_sescmd_cursor(&backend_ref[i]);

            /**
             * Add one waiter to backend reference.
             */
            bref_set_state(get_bref_from_dcb(router_cli_ses,
                                             backend_ref[i].bref_dcb),
                           BREF_WAITING_RESULT);
            /**
             * Start execution if cursor is not already executing.
             * Otherwise, cursor will execute pending commands
             * when it completes with previous commands.
             */
            if (sescmd_cursor_is_active(scur))
            {
                succp = true;

                MXS_INFO("Backend [%s]:%d already executing sescmd.",
                         backend_ref[i].bref_backend->server->name,
                         backend_ref[i].bref_backend->server->port);
            }
            else
            {
                succp = execute_sescmd_in_backend(&backend_ref[i]);

                if (!succp)
                {
                    MXS_ERROR("Failed to execute session "
                              "command in [%s]:%d",
                              backend_ref[i].bref_backend->server->name,
                              backend_ref[i].bref_backend->server->port);
                }
            }
        }
        else
        {
            succp = false;
        }
    }
    /** Unlock router session */
    rses_end_locked_router_action(router_cli_ses);

return_succp:
    return succp;
}

/**
 * Error Handler routine to resolve _backend_ failures. If it succeeds then there
 * are enough operative backends available and connected. Otherwise it fails,
 * and session is terminated.
 *
 * @param       instance        The router instance
 * @param       router_session  The router session
 * @param       errmsgbuf       The error message to reply
 * @param       backend_dcb     The backend DCB
 * @param       action          The action: ERRACT_NEW_CONNECTION or ERRACT_REPLY_CLIENT
 * @param       succp           Result of action: true iff router can continue
 *
 * Even if succp == true connecting to new slave may have failed. succp is to
 * tell whether router has enough master/slave connections to continue work.
 */
static void handleError(MXS_ROUTER* instance,
                        MXS_ROUTER_SESSION* router_session,
                        GWBUF* errmsgbuf,
                        DCB* problem_dcb,
                        mxs_error_action_t action,
                        bool* succp)
{
    MXS_SESSION* session;
    ROUTER_INSTANCE* inst = (ROUTER_INSTANCE *)instance;
    ROUTER_CLIENT_SES* rses = (ROUTER_CLIENT_SES *)router_session;

    CHK_DCB(problem_dcb);

    /** Don't handle same error twice on same DCB */
    if (problem_dcb->dcb_errhandle_called)
    {
        /** we optimistically assume that previous call succeed */
        *succp = true;
        return;
    }
    else
    {
        problem_dcb->dcb_errhandle_called = true;
    }
    session = problem_dcb->session;

    if (session == NULL || rses == NULL)
    {
        *succp = false;
    }
    else if (DCB_ROLE_CLIENT_HANDLER == problem_dcb->dcb_role)
    {
        *succp = false;
    }
    else
    {
        CHK_SESSION(session);
        CHK_CLIENT_RSES(rses);

        switch (action)
        {
        case ERRACT_NEW_CONNECTION:
            {
                if (!rses_begin_locked_router_action(rses))
                {
                    *succp = false;
                    break;
                }
                /**
                 * This is called in hope of getting replacement for
                 * failed slave(s).
                 */
                *succp = handle_error_new_connection(inst,
                                                     rses,
                                                     problem_dcb,
                                                     errmsgbuf);
                rses_end_locked_router_action(rses);
                break;
            }

        case ERRACT_REPLY_CLIENT:
            {
                handle_error_reply_client(session,
                                          rses,
                                          problem_dcb,
                                          errmsgbuf);
                *succp = false; /*< no new backend servers were made available */
                break;
            }

        default:
            *succp = false;
            break;
        }
    }
    dcb_close(problem_dcb);
}


static void handle_error_reply_client(MXS_SESSION*       ses,
                                      ROUTER_CLIENT_SES* rses,
                                      DCB*               backend_dcb,
                                      GWBUF*             errmsg)
{
    mxs_session_state_t sesstate;
    DCB* client_dcb;
    backend_ref_t*  bref;

    sesstate = ses->state;
    client_dcb = ses->client_dcb;

    /**
     * If bref exists, mark it closed
     */
    if ((bref = get_bref_from_dcb(rses, backend_dcb)) != NULL)
    {
        CHK_BACKEND_REF(bref);
        bref_clear_state(bref, BREF_IN_USE);
        bref_set_state(bref, BREF_CLOSED);
    }

    if (sesstate == SESSION_STATE_ROUTER_READY)
    {
        CHK_DCB(client_dcb);
        client_dcb->func.write(client_dcb, gwbuf_clone(errmsg));
    }
}

/**
 * Check if a router session has servers in use
 * @param rses Router client session
 * @return True if session has a single backend server in use that is running.
 * False if no backends are in use or running.
 */
bool have_servers(ROUTER_CLIENT_SES* rses)
{
    int i;

    for (i = 0; i < rses->rses_nbackends; i++)
    {
        if (BREF_IS_IN_USE(&rses->rses_backend_ref[i]) &&
            !BREF_IS_CLOSED(&rses->rses_backend_ref[i]))
        {
            return true;
        }
    }

    return false;
}

/**
 * Check if there is backend reference pointing at failed DCB, and reset its
 * flags. Then clear DCB's callback and finally try to reconnect.
 *
 * This must be called with router lock.
 *
 * @param inst          router instance
 * @param rses          router client session
 * @param dcb           failed DCB
 * @param errmsg        error message which is sent to client if it is waiting
 *
 * @return true if there are enough backend connections to continue, false if not
 */
static bool handle_error_new_connection(ROUTER_INSTANCE*   inst,
                                        ROUTER_CLIENT_SES* rses,
                                        DCB*               backend_dcb,
                                        GWBUF*             errmsg)
{
    MXS_SESSION* ses;
    unsigned char cmd = *((unsigned char*)errmsg->start + 4);

    backend_ref_t* bref;
    bool succp;

    ss_dassert(SPINLOCK_IS_LOCKED(&rses->rses_lock));

    ses = backend_dcb->session;
    CHK_SESSION(ses);

    /**
     * If bref == NULL it has been replaced already with another one.
     */
    if ((bref = get_bref_from_dcb(rses, backend_dcb)) == NULL)
    {
        succp = false;
        goto return_succp;
    }

    CHK_BACKEND_REF(bref);

    /**
     * If query was sent through the bref and it is waiting for reply from
     * the backend server it is necessary to send an error to the client
     * because it is waiting for reply.
     */
    if (BREF_IS_WAITING_RESULT(bref))
    {
        DCB* client_dcb;
        client_dcb = ses->client_dcb;
        client_dcb->func.write(client_dcb, gwbuf_clone(errmsg));
        bref_clear_state(bref, BREF_WAITING_RESULT);
    }
    bref_clear_state(bref, BREF_IN_USE);
    bref_set_state(bref, BREF_CLOSED);

    /**
     * Error handler is already called for this DCB because
     * it's not polling anymore. It can be assumed that
     * it succeed because rses isn't closed.
     */
    if (backend_dcb->state != DCB_STATE_POLLING)
    {
        succp = true;
        goto return_succp;
    }
    /**
     * Remove callback because this DCB won't be used
     * unless it is reconnected later, and then the callback
     * is set again.
     */
    dcb_remove_callback(backend_dcb,
                        DCB_REASON_NOT_RESPONDING,
                        &router_handle_state_switch,
                        (void *)bref);

    /**
     * Try to get replacement slave or at least the minimum
     * number of slave connections for router session.
     */
    succp = connect_backend_servers(rses->rses_backend_ref,
                                    rses->rses_nbackends,
                                    ses,
                                    inst);

    if (!have_servers(rses))
    {
        MXS_ERROR("No more valid servers, closing session");
        succp = false;
        goto return_succp;
    }

return_succp:
    return succp;
}

/**
 * Finds out if there is a backend reference pointing at the DCB given as
 * parameter.
 * @param rses  router client session
 * @param dcb   DCB
 *
 * @return backend reference pointer if succeed or NULL
 */
static backend_ref_t* get_bref_from_dcb(ROUTER_CLIENT_SES* rses,
                                        DCB*               dcb)
{
    backend_ref_t* bref;
    int i = 0;
    CHK_DCB(dcb);
    CHK_CLIENT_RSES(rses);

    bref = rses->rses_backend_ref;

    while (i < rses->rses_nbackends)
    {
        if (bref->bref_dcb == dcb)
        {
            break;
        }
        bref++;
        i += 1;
    }

    if (i == rses->rses_nbackends)
    {
        bref = NULL;
    }
    return bref;
}

/**
 * Calls hang-up function for DCB if it is not both running and in
 * master/slave/joined/ndb role. Called by DCB's callback routine.
 * @param dcb Backend server DCB
 * @param reason The reason this DCB callback was called
 * @param data Data pointer assigned in the add_callback function call
 * @return Always 1
 */
static int router_handle_state_switch(DCB* dcb,
                                      DCB_REASON reason,
                                      void* data)
{
    backend_ref_t* bref;
    int rc = 1;
    SERVER* srv;

    CHK_DCB(dcb);
    if (NULL == dcb->session->router_session)
    {
        /*
         * The following processing will fail if there is no router session,
         * because the "data" parameter will not contain meaningful data,
         * so we have no choice but to stop here.
         */
        return 0;
    }
    bref = (backend_ref_t *) data;
    CHK_BACKEND_REF(bref);

    srv = bref->bref_backend->server;

    if (SERVER_IS_RUNNING(srv))
    {
        goto return_rc;
    }

    switch (reason)
    {
    case DCB_REASON_NOT_RESPONDING:
        atomic_add(&bref->bref_backend->connections, -1);
        MXS_INFO("server %s not responding", srv->unique_name);
        dcb->func.hangup(dcb);
        break;

    default:
        break;
    }

return_rc:
    return rc;
}


static sescmd_cursor_t* backend_ref_get_sescmd_cursor(backend_ref_t* bref)
{
    sescmd_cursor_t* scur;
    CHK_BACKEND_REF(bref);

    scur = &bref->bref_sescmd_cur;
    CHK_SESCMD_CUR(scur);

    return scur;
}

/**
 * Detect if a query contains a SHOW SHARDS query.
 * @param query Query to inspect
 * @return true if the query is a SHOW SHARDS query otherwise false
 */
bool detect_show_shards(GWBUF* query)
{
    bool rval = false;
    char *querystr, *tok, *sptr;

    if (query == NULL)
    {
        MXS_ERROR("NULL value passed at %s:%d", __FILE__, __LINE__);
        return false;
    }

    if (!modutil_is_SQL(query) && !modutil_is_SQL_prepare(query))
    {
        return false;
    }

    if ((querystr = modutil_get_SQL(query)) == NULL)
    {
        MXS_ERROR("Failure to parse SQL at  %s:%d", __FILE__, __LINE__);
        return false;
    }

    tok = strtok_r(querystr, " ", &sptr);
    if (tok && strcasecmp(tok, "show") == 0)
    {
        tok = strtok_r(NULL, " ", &sptr);
        if (tok && strcasecmp(tok, "shards") == 0)
        {
            rval = true;
        }
    }

    MXS_FREE(querystr);
    return rval;
}

struct shard_list
{
    HASHITERATOR* iter;
    ROUTER_CLIENT_SES* rses;
    RESULTSET* rset;
};

/**
 * Callback for the shard list result set creation
 */
RESULT_ROW* shard_list_cb(struct resultset* rset, void* data)
{
    char *key, *value;
    struct shard_list *sl = (struct shard_list*)data;
    RESULT_ROW* rval = NULL;

    if ((key = hashtable_next(sl->iter)) &&
        (value = hashtable_fetch(sl->rses->shardmap->hash, key)))
    {
        if ((rval = resultset_make_row(sl->rset)))
        {
            resultset_row_set(rval, 0, key);
            resultset_row_set(rval, 1, value);
        }
    }
    return rval;
}

/**
 * Send a result set of all shards and their locations to the client.
 * @param rses Router client session
 * @return 0 on success, -1 on error
 */
int process_show_shards(ROUTER_CLIENT_SES* rses)
{
    int rval = 0;

    spinlock_acquire(&rses->shardmap->lock);
    if (rses->shardmap->state != SHMAP_UNINIT)
    {
        HASHITERATOR* iter = hashtable_iterator(rses->shardmap->hash);
        struct shard_list sl;
        if (iter)
        {
            sl.iter = iter;
            sl.rses = rses;
            if ((sl.rset = resultset_create(shard_list_cb, &sl)) == NULL)
            {
                MXS_ERROR("[%s] Error: Failed to create resultset.", __FUNCTION__);
                rval = -1;
            }
            else
            {
                resultset_add_column(sl.rset, "Database", MYSQL_DATABASE_MAXLEN, COL_TYPE_VARCHAR);
                resultset_add_column(sl.rset, "Server", MYSQL_DATABASE_MAXLEN, COL_TYPE_VARCHAR);
                resultset_stream_mysql(sl.rset, rses->rses_client_dcb);
                resultset_free(sl.rset);
                hashtable_iterator_free(iter);
            }
        }
        else
        {
            MXS_ERROR("hashtable_iterator creation failed. "
                      "This is caused by a memory allocation failure.");
            rval = -1;
        }
    }
    spinlock_release(&rses->shardmap->lock);
    return rval;
}

/**
 *
 * @param dcb
 * @param errnum
 * @param mysqlstate
 * @param errmsg
 */
void write_error_to_client(DCB* dcb, int errnum, const char* mysqlstate, const char* errmsg)
{
    GWBUF* errbuff = modutil_create_mysql_err_msg(1, 0, errnum, mysqlstate, errmsg);
    if (errbuff)
    {
        if (dcb->func.write(dcb, errbuff) != 1)
        {
            MXS_ERROR("Failed to write error packet to client.");
        }
    }
    else
    {
        MXS_ERROR("Memory allocation failed when creating error packet.");
    }
}

/**
 *
 * @param router_cli_ses
 * @return
 */
bool handle_default_db(ROUTER_CLIENT_SES *router_cli_ses)
{
    bool rval = false;
    char* target = NULL;

    spinlock_acquire(&router_cli_ses->shardmap->lock);
    if (router_cli_ses->shardmap->state != SHMAP_UNINIT)
    {
        target = hashtable_fetch(router_cli_ses->shardmap->hash, router_cli_ses->connect_db);
    }
    spinlock_release(&router_cli_ses->shardmap->lock);

    if (target)
    {
        /* Send a COM_INIT_DB packet to the server with the right database
         * and set it as the client's active database */

        unsigned int qlen = strlen(router_cli_ses->connect_db);
        GWBUF* buffer = gwbuf_alloc(qlen + 5);

        if (buffer)
        {
            gw_mysql_set_byte3((unsigned char*) buffer->start, qlen + 1);
            gwbuf_set_type(buffer, GWBUF_TYPE_MYSQL);
            *((unsigned char*) buffer->start + 3) = 0x0;
            *((unsigned char*) buffer->start + 4) = 0x2;
            memcpy(buffer->start + 5, router_cli_ses->connect_db, qlen);
            DCB* dcb = NULL;

            if (get_shard_dcb(&dcb, router_cli_ses, target))
            {
                dcb->func.write(dcb, buffer);
                MXS_DEBUG("USE '%s' sent to %s for session %p",
                          router_cli_ses->connect_db,
                          target,
                          router_cli_ses->rses_client_dcb->session);
                rval = true;
            }
            else
            {
                MXS_INFO("Couldn't find target DCB for '%s'.", target);
            }
        }
        else
        {
            MXS_ERROR("Buffer allocation failed.");
        }
    }
    else
    {
        /** Unknown database, hang up on the client*/
        MXS_INFO("Connecting to a non-existent database '%s'",
                 router_cli_ses->connect_db);
        char errmsg[128 + MYSQL_DATABASE_MAXLEN + 1];
        sprintf(errmsg, "Unknown database '%s'", router_cli_ses->connect_db);
        if (router_cli_ses->rses_config.debug)
        {
            sprintf(errmsg + strlen(errmsg), " ([%lu]: DB not found on connect)",
                    router_cli_ses->rses_client_dcb->session->ses_id);
        }
        write_error_to_client(router_cli_ses->rses_client_dcb,
                              SCHEMA_ERR_DBNOTFOUND,
                              SCHEMA_ERRSTR_DBNOTFOUND,
                              errmsg);
    }

    return rval;
}

void route_queued_query(ROUTER_CLIENT_SES *router_cli_ses)
{
    GWBUF* tmp = router_cli_ses->queue;
    router_cli_ses->queue = router_cli_ses->queue->next;
    tmp->next = NULL;
#ifdef SS_DEBUG
    char* querystr = modutil_get_SQL(tmp);
    MXS_DEBUG("Sending queued buffer for session %p: %s",
              router_cli_ses->rses_client_dcb->session,
              querystr);
    MXS_FREE(querystr);
#endif
    poll_add_epollin_event_to_dcb(router_cli_ses->rses_client_dcb, tmp);
}

/**
 *
 * @param router_cli_ses Router client session
 * @return 1 if mapping is done, 0 if it is still ongoing and -1 on error
 */
int inspect_backend_mapping_states(ROUTER_CLIENT_SES *router_cli_ses,
                                   backend_ref_t *bref,
                                   GWBUF** wbuf)
{
    bool mapped = true;
    GWBUF* writebuf = *wbuf;
    backend_ref_t* bkrf = router_cli_ses->rses_backend_ref;

    for (int i = 0; i < router_cli_ses->rses_nbackends; i++)
    {
        if (bref->bref_dcb == bkrf[i].bref_dcb && !BREF_IS_MAPPED(&bkrf[i]))
        {
            if (bref->map_queue)
            {
                writebuf = gwbuf_append(bref->map_queue, writebuf);
                bref->map_queue = NULL;
            }
            showdb_response_t rc = parse_showdb_response(router_cli_ses,
                                                         &router_cli_ses->rses_backend_ref[i],
                                                         &writebuf);
            if (rc == SHOWDB_FULL_RESPONSE)
            {
                router_cli_ses->rses_backend_ref[i].bref_mapped = true;
                MXS_DEBUG("Received SHOW DATABASES reply from %s for session %p",
                          router_cli_ses->rses_backend_ref[i].bref_backend->server->unique_name,
                          router_cli_ses->rses_client_dcb->session);
            }
            else if (rc == SHOWDB_PARTIAL_RESPONSE)
            {
                bref->map_queue = writebuf;
                writebuf = NULL;
                MXS_DEBUG("Received partial SHOW DATABASES reply from %s for session %p",
                          router_cli_ses->rses_backend_ref[i].bref_backend->server->unique_name,
                          router_cli_ses->rses_client_dcb->session);
            }
            else
            {
                DCB* client_dcb = NULL;

                if ((router_cli_ses->init & INIT_FAILED) == 0)
                {
                    if (rc == SHOWDB_DUPLICATE_DATABASES)
                    {
                        MXS_ERROR("Duplicate databases found, closing session.");
                    }
                    else
                    {
                        MXS_ERROR("Fatal error when processing SHOW DATABASES response, closing session.");
                    }
                    client_dcb = router_cli_ses->rses_client_dcb;

                    /** This is the first response to the database mapping which
                     * has duplicate database conflict. Set the initialization bitmask
                     * to INIT_FAILED */
                    router_cli_ses->init |= INIT_FAILED;

                    /** Send the client an error about duplicate databases
                     * if there is a queued query from the client. */
                    if (router_cli_ses->queue)
                    {
                        GWBUF* error = modutil_create_mysql_err_msg(1, 0,
                                                                    SCHEMA_ERR_DUPLICATEDB,
                                                                    SCHEMA_ERRSTR_DUPLICATEDB,
                                                                    "Error: duplicate databases "
                                                                    "found on two different shards.");

                        if (error)
                        {
                            client_dcb->func.write(client_dcb, error);
                        }
                        else
                        {
                            MXS_ERROR("Creating buffer for error message failed.");
                        }
                    }
                }
                *wbuf = writebuf;
                return -1;
            }
        }

        if (BREF_IS_IN_USE(&bkrf[i]) && !BREF_IS_MAPPED(&bkrf[i]))
        {
            mapped = false;
            MXS_DEBUG("Still waiting for reply to SHOW DATABASES from %s for session %p",
                      bkrf[i].bref_backend->server->unique_name,
                      router_cli_ses->rses_client_dcb->session);
        }
    }
    *wbuf = writebuf;
    return mapped ? 1 : 0;
}

/**
 * Replace a shard map with another one. This function copies the contents of
 * the source shard map to the target and frees the source memory.
 * @param target Target shard map to replace
 * @param source Source shard map to use
 */
void replace_shard_map(shard_map_t **target, shard_map_t **source)
{
    shard_map_t *tgt = *target;
    shard_map_t *src = *source;
    tgt->last_updated = src->last_updated;
    tgt->state = src->state;
    hashtable_free(tgt->hash);
    tgt->hash = src->hash;
    MXS_FREE(src);
    *source = NULL;
}

/**
 * Synchronize the router client session shard map with the global shard map for
 * this user.
 *
 * If the router doesn't have a shard map for this user then the current shard map
 * of the client session is added to the router. If the shard map in the router is
 * out of date, its contents are replaced with the contents of the current client
 * session. If the router has a usable shard map, the current shard map of the client
 * is discarded and the router's shard map is used.
 * @param client Router session
 */
void synchronize_shard_map(ROUTER_CLIENT_SES *client)
{
    spinlock_acquire(&client->router->lock);

    client->router->stats.shmap_cache_miss++;

    shard_map_t *map = hashtable_fetch(client->router->shard_maps,
                                       client->rses_client_dcb->user);
    if (map)
    {
        spinlock_acquire(&map->lock);
        if (map->state == SHMAP_STALE)
        {
            replace_shard_map(&map, &client->shardmap);
        }
        else if (map->state != SHMAP_READY)
        {
            MXS_WARNING("Shard map state is not ready but"
                        "it is in use. Replacing it with a newer one.");
            replace_shard_map(&map, &client->shardmap);
        }
        else
        {
            /**
             * Another thread has already updated the shard map for this user
             */
            hashtable_free(client->shardmap->hash);
            MXS_FREE(client->shardmap);
        }
        spinlock_release(&map->lock);
        client->shardmap = map;
    }
    else
    {
        hashtable_add(client->router->shard_maps,
                      client->rses_client_dcb->user,
                      client->shardmap);
        ss_dassert(hashtable_fetch(client->router->shard_maps,
                                   client->rses_client_dcb->user) == client->shardmap);
    }
    spinlock_release(&client->router->lock);
}
