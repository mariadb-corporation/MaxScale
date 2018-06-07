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

static SPINLOCK instlock;
static Avro *instances;

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
    event_types(0),
    event_type_hdr_lens{0},
    binlog_checksum(0),
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
}
