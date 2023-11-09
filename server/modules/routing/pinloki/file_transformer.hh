/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-10-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#pragma once

#include "gtid.hh"
#include <future>
#include <maxbase/ccdefs.hh>
#include <maxbase/stopwatch.hh>
#include <maxbase/compress.hh>
#include <vector>
#include <atomic>
#include <thread>
#include <mutex>

namespace pinloki
{

class Config;

/**
 * @brief FileTransformer runs autonomously in it's own thread. The few public functions
 *        are only accessible via wrapper functions in the Config class. It provides:
 *        - An always up to date list of existing binlog files in creation order.
 *          This list is also written to a file called "binlog.index" for some obscure
 *          reason that the original requirements stipulated.
 *        - Purging of files using "expire_log_duration" if set in config.
 *        - TODO: File compression
 *        - TODO: File archiving
 */
class FileTransformer final
{
public:
    FileTransformer(const Config& config);
    ~FileTransformer();
    std::vector<std::string> binlog_file_names();

    /** The replication state */
    void             set_rpl_state(const maxsql::GtidList& gtids);
    maxsql::GtidList rpl_state();

private:
    int                      m_inotify_fd;
    int                      m_watch;
    maxsql::GtidList         m_rpl_state;
    const Config&            m_config;
    std::vector<std::string> m_file_names;
    std::mutex               m_file_names_mutex;
    std::thread              m_update_thread;
    std::atomic<bool>        m_running{true};
    wall_time::TimePoint     m_next_purge_time;

    std::future<mxb::CompressionStatus> m_compression_future;

    void run();
    void purge_expired_binlogs();
    void update_file_list();
    void update_compression();

    mxb::CompressionStatus compress_file(const std::string& file_name);
    wall_time::TimePoint   oldest_logfile_time();
};

/**
 * @brief PurgeResult enum
 *        Ok               - Files deleted
 *        UpToFileNotFound - The file "up_to" was not found
 *        PartialPurge     - File purge stopped because a file to be purged was in use
 */
enum class PurgeResult
{
    Ok,
    UpToFileNotFound,
    PartialPurge
};

PurgeResult purge_binlogs(const Config& config, const std::string& up_to);
}
