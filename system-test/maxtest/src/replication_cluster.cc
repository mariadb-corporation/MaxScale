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

#include <maxtest/replication_cluster.hh>

using std::string;
namespace
{
const string type_mariadb = "mariadb";
}

namespace maxtest
{
ReplicationCluster::ReplicationCluster(SharedData* shared)
    : MariaDBCluster(shared, "node", "server")
{
}

bool ReplicationCluster::setup(const mxt::NetworkConfig& nwconfig)
{
    return MariaDBCluster::setup(nwconfig);
}

const std::string& ReplicationCluster::type_string() const
{
    return type_mariadb;
}

}
