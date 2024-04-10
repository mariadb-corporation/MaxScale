/*
 * Copyright (c) 2021 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-04-03
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/ccdefs.hh>
#include <maxscale/routingworker.hh>
#include <maxbase/collector.hh>

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

    LogUpdate(const SFile& sFile, std::string&& line, bool flush)
        : sFile(sFile)
        , line(std::move(line))
        , flush(flush)
    {
    }
};

/**
 * @brief The LogContext struct - not used yet
 */
struct LogContext
{
};


using SharedLogLine = maxbase::SharedData<LogContext, LogUpdate>;

class QlaLog : public maxbase::Collector<SharedLogLine, mxb::CollectorMode::UPDATES_ONLY>
             , private maxscale::RoutingWorker::Data
{
public:
    QlaLog();
private:
    void init_for(maxscale::RoutingWorker* pWorker) override final;
    void finish_for(maxscale::RoutingWorker* pWorker) override final;

    void make_updates(LogContext*,
                      std::vector<typename SharedLogLine::UpdateType>& queue) override;
    bool m_error_logged{false};
};
