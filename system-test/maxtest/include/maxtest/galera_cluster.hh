/*
 * Copyright (c) 2021 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-01-25
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#pragma once

#include <maxtest/mariadb_nodes.hh>

class Galera_nodes : public Mariadb_nodes
{
public:

    Galera_nodes(SharedData& shared, const std::string& network_config)
        : Mariadb_nodes(shared, "galera", "gserver", network_config)
    {
    }

    bool setup();

    const std::string& type_string() const override;

    int start_galera();

    virtual int start_replication()
    {
        return start_galera();
    }

    int check_galera();

    virtual int check_replication()
    {
        return check_galera();
    }

    std::string get_config_name(int node) override;

    virtual void sync_slaves(int node = 0)
    {
        sleep(10);
    }
};