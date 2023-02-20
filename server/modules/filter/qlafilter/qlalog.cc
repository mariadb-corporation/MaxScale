/*
 * Copyright (c) 2021 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-03-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#include "qlalog.hh"
#include <maxscale/config.hh>

namespace
{
void log_error(int errnum, const typename SharedLogLine::InternalUpdate& iu)
{
    MXB_SERROR("Failed to write to unified log file "
               << iu.update.sFile->filename
               << ". Error: (" << errnum << ") " << strerror(errnum)
               << ". Suppressing further similar error messages.");
}
}

QlaLog::QlaLog()
    : GCUpdater(new LogContext(),
                0,      // Support dynamic thread count
                10000,  // Queue length.
                0,      // Cap, not used in updates_only mode
                true,   // order updates.
                true)   // Updates only
{
    Data::initialize_workers();
}

void QlaLog::init_for(maxscale::RoutingWorker* pWorker)
{
    increase_client_count(pWorker->index());
}

void QlaLog::finish_for(maxscale::RoutingWorker* pWorker)
{
    decrease_client_count(pWorker->index());
}

/// NOTE: There is a very small caveat with flushing only the last element in the queue.
///       If within the queue the current file changes, then the explicit flush to the
///       current file will happen before the flush of the previous file, which is
///       flushed when the smart_ptr to it is destroyed after this call finishes.
///       The distinction only matters if there is a crash, or qla is used for some
///       sort of debugging.

void QlaLog::make_updates(LogContext*, std::vector<typename SharedLogLine::InternalUpdate>& queue)
{
    for (const auto& e : queue)
    {
        if (!(e.update.sFile->log_stream << e.update.line))
        {
            if (!m_error_logged)
            {
                log_error(errno, e);
                m_error_logged = true;
            }
        }
    }

    const auto& last = queue.back();
    if (last.update.flush)
    {
        if (!last.update.sFile->log_stream.flush() && !m_error_logged)
        {
            log_error(errno, last);
            m_error_logged = true;
        }
    }
}
