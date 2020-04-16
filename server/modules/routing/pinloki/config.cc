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

namespace pinloki
{

std::string Config::path(const std::string& name)
{
    if (name.find_first_of('/') == std::string::npos)
    {
        return config().m_binlog_dir + '/' + name;
    }

    return name;
}

std::string gen_uuid()
{
    return "42";
}

Config::Config()
{
    const auto& d = binlog_dir_path();
    struct stat st = {0};

    if (stat(d.c_str(), &st) == -1)
    {
        MXB_SINFO("Creating directory ");
        mkdir(d.c_str(), 0700);
    }
}

Config& config()
{
    static Config config;
    return config;
}
}
