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
#include <maxbase/ccdefs.hh>
#include <vector>
#include <atomic>
#include <thread>
#include <mutex>

namespace pinloki
{

class Config;

class FileTransformer final
{
public:
    FileTransformer(const Config& config);
    ~FileTransformer();
    void                     set_is_dirty();
    std::vector<std::string> binlog_file_names();

    /** The replication state */
    void             set_rpl_state(const maxsql::GtidList& gtids);
    maxsql::GtidList rpl_state();

private:
    int                      m_inotify_fd;
    int                      m_watch;
    std::atomic<bool>        m_is_dirty{true};
    maxsql::GtidList         m_rpl_state;
    const Config&            m_config;
    std::vector<std::string> m_file_names;
    std::mutex               m_file_names_mutex;
    std::thread              m_update_thread;
    std::atomic<bool>        m_running{true};

    void update();
};
}
