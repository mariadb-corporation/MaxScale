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

#include <maxtest/galera_cluster.hh>
#include <iostream>
#include <maxtest/log.hh>
#include <maxbase/format.hh>

using std::cout;
using std::endl;
using std::string;

namespace
{
const string type_galera = "galera";
}

bool GaleraCluster::setup(const mxt::NetworkConfig& nwconfig)
{
    return MariaDBCluster::setup(nwconfig);
}

int GaleraCluster::start_galera()
{
    bool old_verbose = verbose();
    int local_result = 0;
    local_result += stop_nodes();

    std::stringstream ss;

    for (int i = 0; i < N; i++)
    {
        ss << (i == 0 ? "" : ",") << ip_private(i);
    }

    auto gcomm = ss.str();

    for (int i = 0; i < N; i++)
    {
        // Remove the grastate.dat file

        ssh_node(i, "echo [mysqld] > cluster_address.cnf", true);
        ssh_node_f(i, true, "echo wsrep_cluster_address=gcomm://%s >>  cluster_address.cnf", gcomm.c_str());
        ssh_node(i, "cp cluster_address.cnf /etc/my.cnf.d/", true);
        ssh_node(i, "cp cluster_address.cnf /etc/mysql/my.cnf.d/", true);

        ssh_node(i, "rm -rf /var/lib/mysql/*", true);
        ssh_node(i, "mysql_install_db --user=mysql", true);

        ssh_node_f(i,
                   true,
                   "sed -i 's/###NODE-ADDRESS###/%s/' /etc/my.cnf.d/* /etc/mysql/my.cnf.d/*;"
                   "sed -i \"s|###GALERA-LIB-PATH###|$(ls /usr/lib*/galera*/*.so)|g\" /etc/my.cnf.d/* /etc/mysql/my.cnf.d/*",
                   ip_private(i));
    }

    printf("Starting new Galera cluster\n");
    fflush(stdout);

    // Start the first node that also starts a new cluster
    ssh_node_f(0, true, "galera_new_cluster");

    for (int i = 1; i < N; i++)
    {
        if (start_node(i, "") != 0)
        {
            cout << "Failed to start node" << i << endl;
            cout << "---------- BEGIN LOGS ----------" << endl;
            m_shared.verbose = true;
            ssh_node_f(0, true, "sudo journalctl -u mariadb | tail -n 50");
            cout << "----------- END LOGS -----------" << endl;
        }
    }

    string str = mxb::string_printf("%s/galera_wait_until_ready.sh", m_test_dir.c_str());
    copy_to_node(0, str.c_str(), access_homedir(0));

    ssh_node_f(0, true, "%s/galera_wait_until_ready.sh %s", access_homedir(0), socket_cmd[0].c_str());

    create_users(0);
    const char create_repl_user[] =
        "grant replication slave on *.* to repl@'%%' identified by 'repl'; "
        "FLUSH PRIVILEGES";

    local_result += robust_connect(5) ? 0 : 1;
    local_result += execute_query(nodes[0], "%s", create_repl_user);

    close_connections();
    m_shared.verbose = old_verbose;
    return local_result;
}

int GaleraCluster::check_galera()
{
    int res = 1;

    if (verbose())
    {
        printf("Checking Galera\n");
        fflush(stdout);
    }

    if (connect() == 0)
    {
        Row r = get_row(nodes[0], "SHOW STATUS WHERE Variable_name='wsrep_cluster_size'");

        if (r.size() == 2)
        {
            if (r[1] == std::to_string(N))
            {
                res = 0;
            }
            else
            {
                cout << "Expected cluster size: " << N << " Actual size: " << r[1] << endl;
            }
        }
        else
        {
            cout << "Unexpected result size: "
                 << (r.empty() ? "Empty result" : std::to_string(r.size())) << endl;
        }
    }
    else
    {
        cout << "Failed to connect to the cluster" << endl;
    }

    disconnect();

    return res;
}

std::string GaleraCluster::get_config_name(int node)
{
    std::stringstream ss;
    ss << "galera_server" << node + 1 << ".cnf";
    return ss.str();
}

const std::string& GaleraCluster::type_string() const
{
    return type_galera;
}
