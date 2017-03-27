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

#include "schemarouter.hh"
#include "schemarouterinstance.hh"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <maxscale/alloc.h>
#include <maxscale/buffer.h>
#include <maxscale/log_manager.h>
#include <maxscale/modinfo.h>
#include <maxscale/modutil.h>
#include <maxscale/poll.h>
#include <maxscale/query_classifier.h>
#include <maxscale/router.h>
#include <maxscale/secrets.h>
#include <maxscale/spinlock.h>

using std::string;
using std::map;

#define DEFAULT_REFRESH_INTERVAL "300"

/**
 * @file schemarouter.c The entry points for the simple sharding router module.
 */

SchemaRouter::SchemaRouter(SERVICE *service, char **options):
    mxs::Router<SchemaRouter, SchemaRouterSession>(service)
{
    MXS_CONFIG_PARAMETER* conf;
    MXS_CONFIG_PARAMETER* param;

    /** Add default system databases to ignore */
    this->ignored_dbs.insert("mysql");
    this->ignored_dbs.insert("information_schema");
    this->ignored_dbs.insert("performance_schema");
    this->service = service;
    this->stats.longest_sescmd = 0;
    this->stats.n_hist_exceeded = 0;
    this->stats.n_queries = 0;
    this->stats.n_sescmd = 0;
    this->stats.ses_longest = 0;
    this->stats.ses_shortest = (double)((unsigned long)(~0));
    spinlock_init(&this->lock);

    conf = service->svc_config_param;

    this->schemarouter_config.refresh_databases = config_get_bool(conf, "refresh_databases");
    this->schemarouter_config.refresh_min_interval = config_get_integer(conf, "refresh_interval");
    this->schemarouter_config.debug = config_get_bool(conf, "debug");

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
            throw std::runtime_error("Regex compilation failed");
        }

        pcre2_match_data* match_data = pcre2_match_data_create_from_pattern(re, NULL);

        if (match_data == NULL)
        {
            pcre2_code_free(re);
            throw std::bad_alloc();
        }

        this->ignore_regex = re;
        this->ignore_match_data = match_data;
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
            this->ignored_dbs.insert(tok);
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
            MXS_WARNING("Use of 'max_sescmd_history' is deprecated");
        }
        else if (strcmp(options[i], "disable_sescmd_history") == 0)
        {
            MXS_WARNING("Use of 'disable_sescmd_history' is deprecated");
        }
        else if (strcmp(options[i], "refresh_databases") == 0)
        {
            this->schemarouter_config.refresh_databases = config_truth_value(value);
        }
        else if (strcmp(options[i], "refresh_interval") == 0)
        {
            this->schemarouter_config.refresh_min_interval = atof(value);
        }
        else if (strcmp(options[i], "debug") == 0)
        {
            this->schemarouter_config.debug = config_truth_value(value);
        }
        else
        {
            MXS_ERROR("Unknown router options for %s", options[i]);
            failure = true;
            break;
        }
    }

    if (failure)
    {
        throw std::runtime_error("Failed to create schemarouter instance.");
    }
}

SchemaRouter::~SchemaRouter()
{
    if (this->ignore_regex)
    {
        pcre2_code_free(this->ignore_regex);
    }

    if (this->ignore_match_data)
    {
        pcre2_match_data_free(this->ignore_match_data);
    }
}

SchemaRouter* SchemaRouter::create(SERVICE* pService, char** pzOptions)
{
    return new SchemaRouter(pService, pzOptions);
}

SchemaRouterSession* SchemaRouter::newSession(MXS_SESSION* pSession)
{
    return new SchemaRouterSession(pSession, *this);
}

void SchemaRouter::diagnostics(DCB* dcb)
{
    double sescmd_pct = this->stats.n_sescmd != 0 ?
                        100.0 * ((double)this->stats.n_sescmd / (double)this->stats.n_queries) :
                        0.0;

    /** Session command statistics */
    dcb_printf(dcb, "\n\33[1;4mSession Commands\33[0m\n");
    dcb_printf(dcb, "Total number of queries: %d\n",
               this->stats.n_queries);
    dcb_printf(dcb, "Percentage of session commands: %.2f\n",
               sescmd_pct);
    dcb_printf(dcb, "Longest chain of stored session commands: %d\n",
               this->stats.longest_sescmd);
    dcb_printf(dcb, "Session command history limit exceeded: %d times\n",
               this->stats.n_hist_exceeded);

    /** Session time statistics */

    if (this->stats.sessions > 0)
    {
        dcb_printf(dcb, "\n\33[1;4mSession Time Statistics\33[0m\n");
        dcb_printf(dcb, "Longest session: %.2lf seconds\n", this->stats.ses_longest);
        dcb_printf(dcb, "Shortest session: %.2lf seconds\n", this->stats.ses_shortest);
        dcb_printf(dcb, "Average session length: %.2lf seconds\n", this->stats.ses_average);
    }
    dcb_printf(dcb, "Shard map cache hits: %d\n", this->stats.shmap_cache_hit);
    dcb_printf(dcb, "Shard map cache misses: %d\n", this->stats.shmap_cache_miss);
    dcb_printf(dcb, "\n");
}

uint64_t SchemaRouter::getCapabilities()
{
    return RCAP_TYPE_NONE;
}

MXS_BEGIN_DECLS

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
    static MXS_MODULE info =
    {
        MXS_MODULE_API_ROUTER,
        MXS_MODULE_BETA_RELEASE,
        MXS_ROUTER_VERSION,
        "A database sharding router for simple sharding",
        "V1.0.0",
        RCAP_TYPE_CONTIGUOUS_INPUT,
        &SchemaRouter::s_object,
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
            {MXS_END_MODULE_PARAMS}
        }
    };

    return &info;
}

MXS_END_DECLS
