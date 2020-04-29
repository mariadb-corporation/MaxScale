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

#include "config.hh"
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <maxbase/log.hh>
#include <sstream>
#include <uuid/uuid.h>

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
    &s_spec, "net_timeout", "Network timeout", cfg::INTERPRET_AS_SECONDS, 30s);
}

namespace pinloki
{

// static
mxs::config::Specification& Config::spec()
{
    return s_spec;
}

std::string Config::path(const std::string& name) const
{
    if (name.find_first_of('/') == std::string::npos)
    {
        return m_binlog_dir + '/' + name;
    }

    return name;
}

std::string Config::binlog_dir_path() const
{
    return m_binlog_dir;
}

std::string Config::gtid_file_path() const
{
    return path(m_gtid_file);
}

std::string Config::master_info_file() const
{
    return path(m_master_info_file);
}

std::string Config::inventory_file_path() const
{
    return path(m_binlog_inventory_file);
}

std::string Config::boot_strap_gtid_list() const
{
    return m_boot_strap_gtid_list;
}

void Config::set_boot_strap_gtid_list(const std::string& gtid)
{
    m_boot_strap_gtid_list = gtid;
}

uint32_t Config::server_id() const
{
    return m_server_id;
}

std::chrono::seconds Config::net_timeout() const
{
    return m_net_timeout;
}

std::string gen_uuid()
{
    char uuid_str[36 + 1];
    uuid_t uuid;

    uuid_generate_time(uuid);
    uuid_unparse_lower(uuid, uuid_str);

    return uuid_str;
}

Config::Config(const std::string& name)
    : cfg::Configuration(name, &s_spec)
{
    add_native(&m_binlog_dir, &s_datadir);
    add_native(&m_server_id, &s_server_id);
    add_native(&m_net_timeout, &s_net_timeout);
}
}
