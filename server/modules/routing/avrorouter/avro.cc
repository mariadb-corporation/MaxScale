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

#include <ctype.h>
#include <ini.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <glob.h>
#include <ini.h>
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

#include "avro_converter.hh"

using namespace maxscale;

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
Avro* Avro::create(SERVICE* service, SRowEventHandler handler)
{
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
                return NULL;
            }
        }
        else
        {
            MXS_ERROR("Service '%s' not found.", param->value);
            return NULL;
        }
    }

    return new (std::nothrow) Avro(service, service->svc_config_param, source_service, handler);
}

Avro::Avro(SERVICE* service, MXS_CONFIG_PARAMETER* params, SERVICE* source, SRowEventHandler handler):
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
    task_handle(0),
    event_handler(handler)
{
    /** For detection of CREATE/ALTER TABLE statements */
    static const char* create_table_regex = "(?i)create[a-z0-9[:space:]_]+table";
    static const char* alter_table_regex = "(?i)alter[[:space:]]+table";
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
