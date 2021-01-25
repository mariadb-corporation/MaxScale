/*
 * Copyright (c) 2016 MariaDB Corporation Ab
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

/**
 * @file xpand_nodes.h - work with Xpand setup
 *
 */

#include <cerrno>
#include <string>
#include <maxtest/mariadb_nodes.hh>
#include <maxtest/nodes.hh>


class Xpand_nodes : public Mariadb_nodes
{
public:

    Xpand_nodes(SharedData& shared, std::string network_config)
        : Mariadb_nodes("xpand", shared, network_config, Type::XPAND)
    {
    }

    /**
     * @brief start_cluster Intstalls Xpand on all nodes, configure license, form cluster
     * @return 0 in case of success
     */
    int start_replication();

    /**
     * @brief cnf_servers Generate Xpand servers description for maxscale.cnf
     * @return text for maxscale.cnf
     */
    std::string cnf_servers();

    /**
     * @brief check_replication Checks if Xpand Cluster is up and running
     * @return 0 if Xpand Cluster is ok
     */
    int check_replication();

    /**
     * @brief perpare_server configurs Xpand on the node
     * @param m node index
     * @return 0 in case of success
     */
    int prepare_server(int i);

    std::string block_command(int node) const override;
    std::string unblock_command(int node) const override;
};
