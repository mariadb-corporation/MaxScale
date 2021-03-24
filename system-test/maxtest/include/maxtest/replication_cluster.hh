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

namespace maxtest
{
/**
 * Standard MariaDB master-slave replication cluster.
 */
class ReplicationCluster : public MariaDBCluster
{
public:
    ReplicationCluster(SharedData* shared);

    bool setup(const mxt::NetworkConfig& nwconfig);

    const std::string& type_string() const override;
};
}
