#pragma once
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

#include <maxscale/ccdefs.hh>
#include <maxbase/maxbase.hh>
#include <maxscale/dcb.hh>
#include <maxscale/housekeeper.h>
#include <maxscale/maxscale_test.h>
#include <maxscale/log.hh>
#include <maxscale/config.hh>
#include <maxscale/query_classifier.h>
#include <maxscale/paths.h>
#include <maxscale/alloc.h>
#include <maxscale/routingworker.hh>

#include <sys/stat.h>

#include "../internal/poll.hh"
#include "../internal/modules.hh"

void preload_module(const char* name, const char* path, const char* type)
{
    std::string old_libdir = get_libdir();
    std::string fullpath = TEST_DIR;
    fullpath += "/";
    fullpath += path;
    set_libdir(MXS_STRDUP(fullpath.c_str()));
    load_module(name, type);
    set_libdir(MXS_STRDUP(old_libdir.c_str()));
}

void init_test_env(char* __attribute((unused)) path = nullptr, uint32_t init_type = QC_INIT_BOTH)
{
    config_get_global_options()->n_threads = 1;

    if (!mxs_log_init(NULL, NULL, MXS_LOG_TARGET_STDOUT))
    {
        exit(1);
    }
    atexit(mxs_log_finish);
    dcb_global_init();
    std::string old_libdir = get_libdir();
    set_libdir(MXS_STRDUP(TEST_DIR "/query_classifier/qc_sqlite/"));
    qc_setup(NULL, QC_SQL_MODE_DEFAULT, NULL, NULL);
    qc_process_init(init_type);
    poll_init();
    maxbase::init();
    maxscale::RoutingWorker::init();
    hkinit();
    set_libdir(MXS_STRDUP(old_libdir.c_str()));

    preload_module("mariadbclient", "server/modules/protocol/MySQL/mariadbclient/", MODULE_PROTOCOL);
    preload_module("readconnroute", "server/modules/routing/readconnroute/", MODULE_ROUTER);
    preload_module("mysqlauth", "/server/modules/authenticator/MySQLAuth/", MODULE_AUTHENTICATOR);
}
