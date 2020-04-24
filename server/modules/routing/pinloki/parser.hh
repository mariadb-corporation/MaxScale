/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2023-11-05
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/ccdefs.hh>

#include <string>
#include <vector>

#include "master_config.hh"

namespace parser
{
struct Handler
{
    virtual void select(const std::vector<std::string>& values) = 0;
    virtual void set(const std::string& key, const std::string& value) = 0;

    virtual void change_master_to(const MasterConfig& config) = 0;
    virtual void start_slave() = 0;
    virtual void stop_slave() = 0;
    virtual void reset_slave() = 0;
    virtual void show_slave_status() = 0;
    virtual void show_master_status() = 0;
    virtual void show_binlogs() = 0;
    virtual void show_variables(const std::string& like) = 0;

    virtual void flush_logs() = 0;
    virtual void purge_logs() = 0;

    virtual void error(const std::string& err) = 0;
};

void parse(const std::string& line, Handler* handler);
}
