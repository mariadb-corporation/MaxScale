/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-03-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#pragma once

#include <maxscale/ccdefs.hh>

#include <maxbase/stopwatch.hh>
#include <maxscale/paths.hh>

#include <string>

namespace pinloki
{

std::string gen_uuid();

using namespace std::literals::chrono_literals;
using namespace std::literals::string_literals;


class Config
{
public:
    Config();

    /** Make a full path. This prefixes "name" with m_binlog_dir/,
     *  unless the first character is a forward slash.
     */
    std::string path(const std::string& name) const;

    std::string binlog_dir_path() const;
    std::string inventory_file_path() const;
    std::string gtid_file_path() const;
    /**
     * @brief boot_strap_gtid_list - a.k.a replication state
     * @return
     */
    std::string boot_strap_gtid_list() const;
    uint32_t    server_id() const;

private:
    /** Where the binlog files are stored */
    std::string m_binlog_dir = mxs::datadir() + std::string("/binlogs");
    /** Name of gtid file */
    std::string m_gtid_file = "rpl_state";
    /** Name of the binlog inventory file. */
    std::string m_binlog_inventory_file = "binlog.index";
    /* Hashing directory (properly indexing, but the word is already in use) */
    std::string m_binlog_hash_dir = ".hash";
    /** Gtid used if there in no gtid yet */
    std::string m_boot_strap_gtid_list = "";
    /** Where the current master details are stored */
    std::string m_master_ini_path;
    /** Server id reported to the Master */
    uint32_t m_server_id = 1455;
    /** Server id reported to the slaves */
    int m_master_id = m_server_id;
    /** uuid reported to the server */
    std::string m_uuid = gen_uuid();
    /** uuid reported to the slaves */
    std::string m_master_uuid;
    /** mariadb version reported to the slaves, defaults to the actual master */
    std::string m_master_version;
    /** host name reported to the slaves, defaults to the master's host name */
    std::string m_master_hostname;
    /** If set, m_slave_hostname is sent to the master during registration */
    std::string m_slave_hostname;
    /** Service user */
    std::string m_user = "maxskysql";
    /** Service password */
    std::string m_password = "skysql";
    /** Request master to send a binlog event at this interval , default 5min*/
    maxbase::Duration m_heartbeat_interval = maxbase::Duration(300s);
    /** Max data sent to a lagging slave at a time, default 10Mib.
     *  Does this mean pinloki should buffer things up before sending, but only
     *  up to this amount?
     *  What about buffering up too much to send back to the client - there is already
     *  some kind of method low/high, right?
     *  This will complicate things as inotify should be turned off, then turned on again.
     */
    int m_burst_size = 10 * 1024 * 1024;
    // std::string       m_mariadb10_compatibility; It's always 10.
    // std::string       m_transaction_safety; GTID only, always safe
    /** Each slave will request a heartbeat interval (I think?) */
    bool m_send_slave_heartbeat = true;
    /** Ask the master for semi-sync replication. Only related to master comm. TODO? */
    bool m_semisync = false;
    /** Hm. */
    int  m_ssl_cert_verification_depth = 9;
    bool m_encrypt_binlog = false;
    /**
     *  bool m_encrypt_binlog = false;
     *  std::string m_encryption_algorithm;
     *  std::string m_encryption_key_file;
     */
    // bool mariadb10_master_gtid; GTID only.

    /** Number of connection retries to Master.  What should happen when the retries
     *  have been exhausted?
     */
    int m_master_retry_count = 1000;
    /**
     *  Master connection retyr timout. Default 60s.
     */
    maxbase::Duration m_connect_retry_tmo = 60s;
};

inline std::string Config::binlog_dir_path() const
{
    return m_binlog_dir;
}

inline std::string Config::gtid_file_path() const
{
    return path(m_gtid_file);
}

inline std::string Config::inventory_file_path() const
{
    return path(m_binlog_inventory_file);
}

inline std::string Config::boot_strap_gtid_list() const
{
    return m_boot_strap_gtid_list;
}

inline uint32_t Config::server_id() const
{
    return m_server_id;
}
}
