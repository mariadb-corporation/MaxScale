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

#include "gtid.hh"
#include "shared_binlogs.hh"
#include "file_transformer.hh"

#include <maxscale/ccdefs.hh>

#include <maxbase/stopwatch.hh>
#include <maxbase/temp_file.hh>
#include <maxbase/exception.hh>
#include <maxscale/paths.hh>
#include <maxscale/config2.hh>
#include <maxscale/key_manager.hh>
#include <maxbase/compress.hh>

#include <string>
#include <thread>

namespace pinloki
{

bool has_extension(const std::string& file_name, const std::string& ext);
void strip_extension(std::string& file_name, const std::string& ext);

std::string gen_uuid();

/* File magic numbers. Well known, or registered (zstd) first 4 bytes of a file. */
constexpr size_t MAGIC_SIZE = 4;
static const std::array<char, MAGIC_SIZE> PINLOKI_MAGIC = {char(0xfe), char(0x62), char(0x69), char(0x6e)};
static const std::array<char, MAGIC_SIZE> ZSTD_MAGIC = {char(0x28), char(0xb5), char(0x2f), char(0xfd)};

// zstd a.k.a. Zstandard compression
static const std::string COMPRESSION_EXTENSION{"zst"};
// A file that is being compressed into
static const std::string COMPRESSION_ONGOING_EXTENSION{"compressing"};
// Subdirectory to binlogdir used during compression
static const std::string COMPRESSION_DIR = "compression";

DEFINE_EXCEPTION(BinlogReadError);
DEFINE_EXCEPTION(GtidNotFoundError);

enum class ExpirationMode
{
    PURGE,
    ARCHIVE
};

struct FileLocation
{
    std::string file_name;
    long        loc;
};

class Config : public mxs::config::Configuration
{
public:
    Config(const std::string& name, std::function<bool()> callback);
    Config(Config&&) = delete;

    static const mxs::config::Specification* spec();

    std::string binlog_dir() const;

    // Full path to the compression dir
    std::string compression_dir() const;

    /** Make a full path. This prefixes "name" with m_binlog_dir/,
     *  unless the first character is a forward slash.
     */
    std::string path(const std::string& name) const;

    std::string              inventory_file_path() const;
    std::string              gtid_file_path() const;
    std::string              requested_gtid_file_path() const;
    std::string              master_info_file() const;
    uint32_t                 server_id() const;
    std::vector<std::string> binlog_file_names() const;

    /** The replication state */
    void             save_rpl_state(const maxsql::GtidList& gtids) const;
    maxsql::GtidList rpl_state() const;

    // Network timeout
    std::chrono::seconds net_timeout() const;
    // Automatic master selection
    bool select_master() const;
    bool ddl_only() const;
    void disable_select_master();

    const std::string& key_id() const;

    mxb::Cipher::AesMode encryption_cipher() const;

    bool semi_sync() const;

    // File purging
    ExpirationMode      expiration_mode() const;
    std::string         archivedir() const;
    int32_t             expire_log_minimum_files() const;
    wall_time::Duration expire_log_duration() const;
    wall_time::Duration purge_startup_delay() const;
    wall_time::Duration purge_poll_timeout() const;

    // Compression
    mxb::CompressionAlgorithm compression_algorithm() const;
    int32_t number_of_noncompressed_files() const;

    static const maxbase::TempDirectory& pinloki_temp_dir();

    const SharedBinlogFile& shared_binlog_file() const;

    bool post_configure(const std::map<std::string, mxs::ConfigParameters>& nested_params) override;

private:
    /** Where the binlog files are stored */
    std::string m_binlog_dir;
    /** Where the binlogs are compressed, as in being compressed. */
    std::string m_compression_dir;
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

    /**
     *  Master connection retry timout. Default 60s.
     */
    maxbase::Duration m_connect_retry_tmo = 60s;

    std::chrono::seconds m_net_timeout;
    bool                 m_select_master;
    bool                 m_select_master_disabled {false};
    bool                 m_ddl_only {false};
    std::string          m_encryption_key_id;
    mxb::Cipher::AesMode m_encryption_cipher;

    ExpirationMode            m_expiration_mode;
    std::string               m_archivedir;
    int64_t                   m_expire_log_minimum_files;
    wall_time::Duration       m_expire_log_duration;
    wall_time::Duration       m_purge_startup_delay;
    wall_time::Duration       m_purge_poll_timeout;
    mxb::CompressionAlgorithm m_compression_algorithm;
    int64_t                   m_number_of_noncompressed_files;

    bool                m_semi_sync;

    std::function<bool()> m_cb;

    std::unique_ptr<FileTransformer> m_sFile_transformer;
    SharedBinlogFile m_shared_binlog_file;
};

inline std::string Config::binlog_dir() const
{
    return m_binlog_dir;
}

inline std::string Config::compression_dir() const
{
    return m_compression_dir;
}

inline const SharedBinlogFile &Config::shared_binlog_file() const
{
    return m_shared_binlog_file;
}
}
