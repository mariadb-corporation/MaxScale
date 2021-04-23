/*
 * Copyright (c) 2021 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-03-24
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
    GaleraCluster(mxt::SharedData* shared);

    bool start_replication() override;

    std::string get_srv_cnf_filename(int node) override;

    const std::string& type_string() const override;
    const std::string& nwconf_prefix() const override;
    const std::string& name() const override;

private:
    bool check_replication() override;
    bool reset_server(int i) override;
};
