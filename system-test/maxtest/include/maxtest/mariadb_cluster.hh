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

/**
 * Standard MariaDB master-slave replication cluster.
 */
class MariaDBCluster : public Mariadb_nodes
{
public:
    MariaDBCluster(SharedData& shared, const std::string& network_config);

    bool setup();

    const std::string& type_string() const override;
};