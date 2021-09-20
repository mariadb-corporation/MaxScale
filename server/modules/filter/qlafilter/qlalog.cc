/*
 * Copyright (c) 2021 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-08-17
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#include "qlalog.hh"
#include <maxscale/config.hh>

QlaLog::QlaLog()
    : GCUpdater(new LogContext(),
                config_threadcount(),
                10000,                      // Queue length.
                0,                          // Cap, not used in updates_only mode
                true,                       // order updates.
                true)                       // Updates only
{
}

void QlaLog::make_updates(LogContext*, std::vector<typename SharedLogLine::InternalUpdate>& queue)
{
    bool error = m_write_error_happened.load(std::memory_order_acquire);

    for (const auto& e : queue)
    {
        error = (fprintf(e.update.sFile.get(), "%s", e.update.line.c_str()) < 0) | error;
        if (e.update.flush)
        {
            error = (fflush(e.update.sFile.get()) != 0) | error;
        }
    }

    m_write_error_happened.store(error, std::memory_order_release);
}
