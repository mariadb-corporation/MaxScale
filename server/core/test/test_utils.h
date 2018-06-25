#pragma once
#ifndef TEST_UTILS_H
#define TEST_UTILS_H
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

#include <maxscale/cdefs.h>
#include <maxscale/dcb.h>
#include <maxscale/housekeeper.h>
#include <maxscale/maxscale_test.h>
#include <maxscale/log_manager.h>
#include <maxscale/config.h>
#include <maxscale/query_classifier.h>
#include <maxscale/paths.h>
#include <maxscale/alloc.h>

#include <sys/stat.h>

#include "../internal/poll.h"
#include "../internal/routingworker.hh"
#include "../internal/statistics.h"


void init_test_env(char *path)
{
    config_get_global_options()->n_threads = 1;

    ts_stats_init();
    if (!mxs_log_init(NULL, NULL, MXS_LOG_TARGET_STDOUT))
    {
        exit(1);
    }
    dcb_global_init();
    set_libdir(MXS_STRDUP(TEST_DIR "/query_classifier/qc_sqlite/"));
    qc_setup(NULL, QC_SQL_MODE_DEFAULT, NULL);
    qc_process_init(QC_INIT_BOTH);
    poll_init();
    maxscale::MessageQueue::init();
    maxscale::Worker::init();
    maxscale::RoutingWorker::init();
    hkinit();
}

#endif
