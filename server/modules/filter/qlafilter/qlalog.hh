/*
 * Copyright (c) 2021 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-11-19
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/ccdefs.hh>
#include <maxbase/gcupdater.hh>

#include <fstream>

struct LogFile
{
    LogFile(const std::string& filename, std::ios_base::openmode mode)
        : log_stream(filename, mode)
        , filename(filename)
    {
    }

    LogFile() = default;
    LogFile(LogFile&&) = default;
    LogFile& operator=(LogFile&&) = default;

    bool is_open()
    {
        return log_stream.is_open();
    }

    std::ofstream log_stream;
    std::string   filename;
};

using SFile = std::shared_ptr<LogFile>;

struct LogUpdate
{
    SFile       sFile;
    std::string line;
    bool        flush;
};

/**
 * @brief The LogContext struct - not used yet
 */
struct LogContext
{
};


using SharedLogLine = maxbase::SharedData<LogContext, LogUpdate>;

class QlaLog : public maxbase::GCUpdater<SharedLogLine>
{
public:
    QlaLog();
private:
    void make_updates(LogContext*,
                      std::vector<typename SharedLogLine::InternalUpdate>& queue) override;
    bool m_error_logged{false};
};
