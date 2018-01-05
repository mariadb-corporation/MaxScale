#pragma once
#ifndef TEST_UTILS_H
#define TEST_UTILS_H
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

#include <maxscale/cdefs.h>
#include <maxscale/dcb.h>
#include <maxscale/housekeeper.h>
#include <maxscale/maxscale_test.h>
#include <maxscale/log_manager.h>
#include <maxscale/config.h>
#include <maxscale/query_classifier.h>

#include "../maxscale/poll.h"
#include "../maxscale/statistics.h"


void init_test_env(char *path)
{
    int argc = 3;

    const char* logdir = path ? path : TEST_LOG_DIR;

    config_get_global_options()->n_threads = 1;

    ts_stats_init();
    mxs_log_init(NULL, logdir, MXS_LOG_TARGET_DEFAULT);
    dcb_global_init();
    qc_setup(NULL, NULL);
    qc_process_init(QC_INIT_BOTH);
    poll_init();
    hkinit();
}

#endif
