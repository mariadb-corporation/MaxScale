/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-10-11
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#pragma once

#include "gtid.hh"

#include <maxscale/ccdefs.hh>

#include <maxbase/stopwatch.hh>
#include <maxscale/paths.hh>
#include <maxscale/config2.hh>

#include <string>

namespace pinloki
{

std::string gen_uuid();

using namespace std::literals::chrono_literals;
using namespace std::literals::string_literals;

class Config : public mxs::config::Configuration
{
public:
    Config(const std::string& name);

    Config(Config&&) = default;

    static mxs::config::Specification& spec();

    /** Make a full path. This prefixes "name" with m_binlog_dir/,
     *  unless the first character is a forward slash.
     */
    std::string path(const std::string& name) const;

    std::string binlog_dir_path() const;
    std::string inventory_file_path() const;
    std::string gtid_file_path() const;
    std::string requested_gtid_file_path() const;
    std::string master_info_file() const;
    uint32_t    server_id() const;

    // Network timeout
    std::chrono::seconds net_timeout() const;
    // Automatic master selection
    bool select_master() const;
    void disable_select_master();

    // File purging
    int32_t             expire_log_minimum_files() const;
    wall_time::Duration expire_log_duration() const;
    wall_time::Duration purge_startup_delay() const;
    wall_time::Duration purge_poll_timeout() const;
private:
    /** Where the binlog files are stored */
    std::string m_binlog_dir;
    /** Name of gtid file */
    std::string m_gtid_file = "rpl_state";
    /** Master configuration file name */
    std::string m_master_info_file = "master-info.json";
    /** Name of the binlog inventory file. */
    std::string m_binlog_inventory_file = "binlog.index";
    /* Hashing directory (properly indexing, but the word is already in use) */
    std::string m_binlog_hash_dir = ".hash";
    /** Where the current master details are stored */
    std::string m_master_ini_path;
    /** Server id reported to the Master */
    int64_t m_server_id;
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

    std::chrono::seconds m_net_timeout;
    bool                 m_select_master;
    bool                 m_select_master_disabled {false};

    int64_t             m_expire_log_minimum_files;
    wall_time::Duration m_expire_log_duration;
    wall_time::Duration m_purge_startup_delay;
    wall_time::Duration m_purge_poll_timeout;
};
}
