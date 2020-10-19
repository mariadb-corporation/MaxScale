/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-10-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

/**
 * @file clustrix_nodes.h - work with Clustrix setup
 *
 * ~/.config/mdbci/clustrix_license file have to contain SQL
 * which setups license to the Clustrix node
 *
 * TODO: move functionality of install_clustrix() to MDBCI
 */

#include <cerrno>
#include <string>
#include <maxtest/mariadb_nodes.hh>
#include <maxtest/nodes.hh>

#define CLUSTRIX_DEPS_YUM "yum install -y bzip2 wget screen ntp ntpdate vim htop mdadm"
#define WGET_CLUSTRIX     "wget http://files.clustrix.com/releases/software/clustrix-9.1.4.el7.tar.bz2"
#define UNPACK_CLUSTRIX   "tar xvjf clustrix-9.1.4.el7.tar.bz2"
#define INSTALL_CLUSTRIX  "cd clustrix-9.1.4.el7; sudo ./clxnode_install.py --yes --force"

class Clustrix_nodes : public Mariadb_nodes
{
public:

    Clustrix_nodes(const char* pref, const char* test_cwd, bool verbose, std::string network_config)
        : Mariadb_nodes(pref, test_cwd, verbose, network_config)
    {
    }

    /**
     * @brief start_cluster Intstalls Clustrix on all nodes, configure license, form cluster
     * @return 0 in case of success
     */
    int start_replication();

    /**
     * @brief cnf_servers Generate Clustrix servers description for maxscale.cnf
     * @return text for maxscale.cnf
     */
    std::string cnf_servers();

    /**
     * @brief check_replication Checks if Clustrix Cluster is up and running
     * @return 0 if Clustrix Cluster is ok
     */
    int check_replication();

    /**
     * @brief install_clustrix
     * @param m node index
     * @return 0 in case of success
     */
    int prepare_server(int i);

    std::string block_command(int node) const override;
    std::string unblock_command(int node) const override;
};
