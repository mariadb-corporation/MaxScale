/*
 * Copyright (c) 2020 MariaDB Corporation Ab
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

#include <array>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

#include <maxbase/exception.hh>
#include <maxbase/worker.hh>

#include "pinloki.hh"
#include "gtid.hh"
#include "inventory.hh"
#include "rpl_event.hh"
#include "find_gtid.hh"
#include "binlog_file.hh"

namespace pinloki
{
/**
 * @brief FileReader - Provide events from files starting at a given Gtid. Once all events
 *                     have been read, FileReader sets up inotify-epoll notifications for
 *                     changes to the last (active) file.
 */
class FileReader    // : public Storage
{
public:
    FileReader(const maxsql::GtidList& gtid_list, const InventoryReader* inv);
    ~FileReader();

    maxsql::RplEvent fetch_event(const maxbase::Timer& timer);

    // Artificial replication heartbeat event
    maxsql::RplEvent create_heartbeat_event() const;

    /**
     * @brief fd - file descriptor that this reader want's to epoll
     * @return file descriptor
     */
    int fd();

    /**
     * @brief fd_notify - the Worker calls this when the file descriptor fd()
     *                    has events (what events? TODO need to provide that as well).
     * @param events
     */
    void fd_notify(uint32_t events);

    /* Called from generate_heartbeats, to generate possible decompression error */
    void check_status();
private:
    struct ReadPosition
    {
        std::string                 rotate_name;    // the file name as read from the binlog
        std::shared_ptr<BinlogFile> sBinlog;
        IFStreamReader              file;
        int64_t                     next_pos;
    };

    void             open(const std::string& rotate_name);
    void             set_inotify_fd();
    maxsql::RplEvent fetch_event_internal();

    int                    m_inotify_fd;
    int                    m_inotify_descriptor = -1;
    ReadPosition           m_read_pos;
    uint32_t               m_server_id;
    const InventoryReader& m_inventory;
    std::string            m_generate_rotate_to;
    bool                   m_generating_preamble = true;
    int                    m_initial_gtid_file_pos = 0;

    std::vector<GtidPosition> m_catchup;
    std::set<uint32_t>        m_active_domains;
    bool                      m_skip_gtid = false;

    std::unique_ptr<mxq::EncryptCtx> m_encrypt;
};
}
