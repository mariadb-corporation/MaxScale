/**
 * @file clustrix_nodes.h - work with Clustrix setup
 *
 * ~/.config/mdbci/clustrix_license file have to contain SQL
 * which setups license to the Clustrix node
 *
 * TODO: move functionality of install_clustrix() to MDBCI
 */



#pragma once

#include <errno.h>
#include <string>
#include "nodes.h"
#include "mariadb_nodes.h"

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
     * @brief install_clustrix
     * @param m node index
     * @return 0 in case of success
     */
    int install_clustrix(int m);

    /**
     * @brief start_cluster Intstalls Clustrix on all nodes, configure license, form cluster
     * @return 0 in case of success
     */
    int start_cluster();

    /**
     * @brief cnf_servers Generate Clustrix servers description for maxscale.cnf
     * @return text for maxscale.cnf
     */
    std::string cnf_servers();
};
