/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-05-25
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include "csmon.hh"
#include <maxscale/config2.hh>
#include "columnstore.hh"

class CsConfig : public mxs::config::Configuration
{
public:
    CsConfig(const std::string& name);

    static void populate(MXS_MODULE& info);

    cs::Version               version;                  // Optional for 1.5
    int64_t                   admin_port;               // Optional for 1.5
    std::string               admin_base_path;          // Optional for 1.5
    std::string               api_key;                  // Optional for 1.5
    std::string               local_address;            // Mandatory (unless global exists) for 1.5
    bool                      dynamic_node_detection;   // Optional for 1.5
    std::chrono::milliseconds cluster_monitor_interval; // Optional for 1.5

private:
    bool post_configure();

    bool check_api_key(const std::string& dir);
    bool check_mandatory();
};
