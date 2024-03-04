/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-02-27
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

// NOTE: Do not include <maxscale/ccdefs.hh>, it includes this.
#include <maxbase/ccdefs.hh>

#if defined (MXS_MODULE_NAME)
#error In MaxScale >= 7, a module must not declare MXS_MODULE_NAME, but MXB_MODULE_NAME.
#endif

#if !defined (MXB_MODULE_NAME)
#define MXB_MODULE_NAME NULL
#endif

#include <maxbase/log.hh>

/**
 * Initializes MaxScale log manager
 *
 * @param ident  The syslog ident. If NULL, then the program name is used.
 * @param logdir The directory for the log file. If NULL, file output is discarded.
 * @param target Logging target
 *
 * @return true if succeed, otherwise false
 */
bool mxs_log_init(const char* ident, const char* logdir, mxb_log_target_t target);

/**
 * Close and reopen MaxScale log files. Also increments a global rotation counter which modules
 * can read to see if they should rotate their own logs.
 *
 * @return True if MaxScale internal logs were rotated. If false is returned, the rotation counter is not
 * incremented.
 */
bool mxs_log_rotate();

/**
 * Get the value of the log rotation counter. The counter is incremented when user requests a log rotation.
 *
 * @return Counter value
 */
int mxs_get_log_rotation_count();

inline void mxs_log_finish()
{
    mxb_log_finish();
}
