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

#include "config.hh"
#include "pinloki.hh"
#include "file_transformer.hh"

#include <maxbase/log.hh>
#include <maxscale/utils.hh>

#include <sstream>
#include <fstream>
#include <uuid/uuid.h>
#include <sys/stat.h>
#include <unistd.h>

namespace
{
namespace cfg = maxscale::config;
using namespace std::literals::chrono_literals;

cfg::Specification s_spec("pinloki", cfg::Specification::ROUTER);

cfg::ParamPath s_datadir(
    &s_spec, "datadir", "Directory where binlog files are stored",
    cfg::ParamPath::C | cfg::ParamPath::W | cfg::ParamPath::R | cfg::ParamPath::X,
    mxs::datadir() + std::string("/binlogs/"));

cfg::ParamCount s_server_id(
    &s_spec, "server_id", "Server ID sent to both slaves and the master", 1234);

cfg::ParamSeconds s_net_timeout(
    &s_spec, "net_timeout", "Network timeout", 10s);

cfg::ParamBool s_select_master(
    &s_spec, "select_master", "Automatically select the master server", false);

cfg::ParamBool s_ddl_only(
    &s_spec, "ddl_only", "Ignore data events and only keep DDL events", false);

cfg::ParamString s_encryption_key_id(
    &s_spec, "encryption_key_id", "Key ID used for binlog encryption", "");

cfg::ParamEnum<mxb::Cipher::AesMode> s_encryption_cipher(
    &s_spec, "encryption_cipher", "Binlog encryption algorithm",
    {
        {mxb::Cipher::AES_CBC, "AES_CBC"},
        {mxb::Cipher::AES_CTR, "AES_CTR"},
        {mxb::Cipher::AES_GCM, "AES_GCM"},
    },
    mxb::Cipher::AES_GCM);

cfg::ParamCount s_expire_log_minimum_files(
    &s_spec, "expire_log_minimum_files", "Minimum number of files the automatic log purge keeps", 2);

cfg::ParamDuration<wall_time::Duration> s_expire_log_duration(
    &s_spec, "expire_log_duration", "Duration after which unmodified log files are purged",
    0s);

/* Undocumented config items (for test purposes) */
cfg::ParamDuration<wall_time::Duration> s_purge_startup_delay(
    &s_spec, "purge_startup_delay", "Purge waits this long after a MaxScale startup",
    2min);

cfg::ParamDuration<wall_time::Duration> s_purge_poll_timeout(
    &s_spec, "purge_poll_timeout", "Purge timeout/poll when expire_log_minimum_files files exist",
    2min);

cfg::ParamBool s_rpl_semi_sync_slave_enabled(
    &s_spec, "rpl_semi_sync_slave_enabled", "Enable semi-synchronous replication", false);
}

namespace pinloki
{

bool has_extension(const std::string& file_name, const std::string& ext)
{
    if (auto pos = file_name.find_last_of(".");
        pos != std::string::npos
        && file_name.substr(pos + 1, std::string::npos) == ext)
    {
        return true;
    }
    else
    {
        return false;
    }
}

void strip_extension(std::string& file_name, const std::string& ext)
{
    if (has_extension(file_name, ext))
    {
        file_name.resize(file_name.size() - ext.size() - 1);
    }
}

// static
const mxs::config::Specification* Config::spec()
{
    return &s_spec;
}

std::string Config::path(const std::string& name) const
{
    if (name.find_first_of('/') == std::string::npos)
    {
        return m_binlog_dir + '/' + name;
    }

    return name;
}

std::string Config::gtid_file_path() const
{
    return path(m_gtid_file);
}

std::string Config::requested_gtid_file_path() const
{
    return path("requested_rpl_state");
}

std::string Config::master_info_file() const
{
    return path(m_master_info_file);
}

std::string Config::inventory_file_path() const
{
    return path(m_binlog_inventory_file);
}

uint32_t Config::server_id() const
{
    return m_server_id;
}

std::chrono::seconds Config::net_timeout() const
{
    return m_net_timeout;
}

bool Config::select_master() const
{
    return m_select_master && !m_select_master_disabled;
}

bool Config::ddl_only() const
{
    return m_ddl_only;
}

void Config::disable_select_master()
{
    m_select_master_disabled = true;
}

int32_t Config::expire_log_minimum_files() const
{
    return m_expire_log_minimum_files;
}

wall_time::Duration Config::expire_log_duration() const
{
    return m_expire_log_duration;
}

wall_time::Duration Config::purge_startup_delay() const
{
    return m_purge_startup_delay;
}


wall_time::Duration Config::purge_poll_timeout() const
{
    return m_purge_poll_timeout;
}

const std::string& Config::key_id() const
{
    return m_encryption_key_id;
}

// static
const maxbase::TempDirectory &Config::pinloki_temp_dir()
{
    static maxbase::TempDirectory pinloki_temp_dir("/tmp/pinloki_tmp");

    return pinloki_temp_dir;
}

mxb::Cipher::AesMode Config::encryption_cipher() const
{
    return m_encryption_cipher;
}

bool Config::semi_sync() const
{
    return m_semi_sync;
}

std::string gen_uuid()
{
    char uuid_str[36 + 1];
    uuid_t uuid;

    uuid_generate_time(uuid);
    uuid_unparse_lower(uuid, uuid_str);

    return uuid_str;
}

bool Config::post_configure(const std::map<std::string, mxs::ConfigParameters>& nested_params)
{
    bool ok = false;

    // This is a workaround to the fact that the datadir is not created if the default value is used.
    if (mxs_mkdir_all(m_binlog_dir.c_str(), S_IWUSR | S_IWGRP | S_IRUSR | S_IRGRP | S_IXUSR | S_IXGRP))
    {
        m_sFile_transformer.reset(new FileTransformer(*this));
        ok = m_cb();
    }

    return ok;
}

Config::Config(const std::string& name, std::function<bool()> callback)
    : cfg::Configuration(name, &s_spec)
    , m_cb(callback)
{
    add_native(&Config::m_binlog_dir, &s_datadir);
    add_native(&Config::m_server_id, &s_server_id);
    add_native(&Config::m_net_timeout, &s_net_timeout);
    add_native(&Config::m_select_master, &s_select_master);
    add_native(&Config::m_ddl_only, &s_ddl_only);
    add_native(&Config::m_encryption_key_id, &s_encryption_key_id);
    add_native(&Config::m_encryption_cipher, &s_encryption_cipher);
    add_native(&Config::m_expire_log_duration, &s_expire_log_duration);
    add_native(&Config::m_expire_log_minimum_files, &s_expire_log_minimum_files);
    add_native(&Config::m_purge_startup_delay, &s_purge_startup_delay);
    add_native(&Config::m_purge_poll_timeout, &s_purge_poll_timeout);
    add_native(&Config::m_semi_sync, &s_rpl_semi_sync_slave_enabled);
}

std::vector<std::string> Config::binlog_file_names() const
{
    return m_sFile_transformer->binlog_file_names();
}

void Config::set_binlogs_dirty() const
{
    m_sFile_transformer->set_is_dirty();
}

void Config::save_rpl_state(const maxsql::GtidList& gtids) const
{
    m_sFile_transformer->set_rpl_state(gtids);
}

maxsql::GtidList Config::rpl_state() const
{
    return m_sFile_transformer->rpl_state();
}
}
