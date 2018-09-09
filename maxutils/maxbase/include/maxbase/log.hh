/*
 * Copyright (c) 2018 MariaDB Corporation Ab
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
#pragma once

#include <maxbase/ccdefs.hh>
#include <stdexcept>
#include <maxbase/log.h>

/**
 * @brief Initialize the log
 *
 * This function initializes the log using
 * - the program name as the syslog ident,
 * - the current directory as the logdir, and
 * - the default log name (program name + ".log").
 *
 * @param target  The specified target for the logging.
 *
 * @return True if succeeded, false otherwise.
 */
inline bool mxb_log_init(mxb_log_target_t target = MXB_LOG_TARGET_FS)
{
    return mxb_log_init(nullptr, ".", nullptr, target, nullptr);
}

namespace maxbase
{

/**
 * @class Log
 *
 * A simple utility RAII class where the constructor initializes the log and
 * the destructor finalizes it.
 */
class Log
{
    Log(const Log&) = delete;
    Log& operator=(const Log&) = delete;

public:
    Log(const char* ident,
        const char* logdir,
        const char* filename,
        mxb_log_target_t target,
        mxb_log_context_provider_t context_provider)
    {
        if (!mxb_log_init(ident, logdir, filename, target, context_provider))
        {
            throw std::runtime_error("Failed to initialize the log.");
        }
    }

    Log(mxb_log_target_t target = MXB_LOG_TARGET_FS)
        : Log(nullptr, ".", nullptr, target, nullptr)
    {
    }

    ~Log()
    {
        mxb_log_finish();
    }
};
}
