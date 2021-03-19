/*
 * Copyright (c) 2021 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-03-08
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#pragma once

#include <maxtest/mariadb_nodes.hh>

class GaleraCluster : public MariaDBCluster
{
public:

    GaleraCluster(mxt::SharedData* shared)
        : MariaDBCluster(shared, "galera", "gserver")
    {
    }

    const std::string& type_string() const override;

    int start_replication() override;
    int check_replication() override;

    std::string get_config_name(int node) override;

    virtual void sync_slaves(int node = 0) override
    {
        sleep(10);
    }
};