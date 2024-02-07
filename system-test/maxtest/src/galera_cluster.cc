/*
 * Copyright (c) 2021 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
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
using mxt::MariaDBServer;

namespace
{
const string type_galera = "galera";
const string my_nwconf_prefix = "galera";
const string my_name = "Galera-cluster";
}

GaleraCluster::GaleraCluster(mxt::SharedData* shared)
    : MariaDBCluster(shared, "gserver")
{
}

bool GaleraCluster::start_replication()
{
    int local_result = stop_nodes() ? 0 : 1;

    std::stringstream ss;

    for (int i = 0; i < N; i++)
    {
        ss << (i == 0 ? "" : ",") << ip_private(i);
    }

    auto gcomm = ss.str();

    run_on_every_backend([&](int i){
        copy_to_node(i,
                     (mxt::SOURCE_DIR + std::string("/mdbci/cnf/") + get_srv_cnf_filename(i)).c_str(),
                     access_homedir(i));
        ssh_node(i,
                 (std::string("cp ") + get_srv_cnf_filename(i) + std::string(" /etc/my.cnf.d/")).c_str(),
                  true);
        ssh_node(i, "echo [mysqld] > cluster_address.cnf", true);
        ssh_node_f(i, true, "echo wsrep_cluster_address=gcomm://%s >>  cluster_address.cnf", gcomm.c_str());
        ssh_node(i, "cp cluster_address.cnf /etc/my.cnf.d/", true);
        ssh_node(i, "cp cluster_address.cnf /etc/mysql/my.cnf.d/", true);

        ssh_node(i, "rm -rf /var/lib/mysql/*", true);
        unblock_node_port(i, 4567);
        unblock_node_port(i, 4568);
        unblock_node_port(i, 4444);
        ssh_node(i, "mariadb-install-db --user=mysql", true);

        ssh_node_f(i,
                   true,
                   "sed -i 's/###NODE-ADDRESS###/%s/' /etc/my.cnf.d/* /etc/mysql/my.cnf.d/*;"
                   "sed -i \"s|###GALERA-LIB-PATH###|$(ls /usr/lib*/galera*/*.so)|g\" /etc/my.cnf.d/* /etc/mysql/my.cnf.d/*",
                   ip_private(i));
        return true;
    });

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
            cout << ssh_output("sudo journalctl -u mariadb | tail -n 50", i, true).output;
            cout << "----------- END LOGS -----------" << endl;
        }
    }

    string str = mxb::string_printf("%s/galera_wait_until_ready.sh", m_test_dir.c_str());
    copy_to_node(0, str.c_str(), access_homedir(0));

    ssh_node_f(0, true, "%s/galera_wait_until_ready.sh %s", access_homedir(0), m_socket_cmd[0].c_str());

    create_users(0);
    const char create_repl_user[] =
        "grant replication slave on *.* to repl@'%%' identified by 'repl'; "
        "FLUSH PRIVILEGES";

    local_result += robust_connect(5) ? 0 : 1;
    local_result += execute_query(nodes[0], "%s", create_repl_user);

    close_connections();
    return local_result == 0;
}

bool GaleraCluster::check_replication()
{
    bool res = false;

    if (verbose())
    {
        printf("Checking Galera\n");
        fflush(stdout);
    }

    if (connect("") == 0)
    {
        Row r = get_row(nodes[0], "SHOW STATUS WHERE Variable_name='wsrep_cluster_size'");

        if (r.size() == 2)
        {
            if (r[1] == std::to_string(N))
            {
                res = true;
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

std::string GaleraCluster::get_srv_cnf_filename(int node)
{
    return mxb::string_printf("galera_server%i.cnf", node + 1);
}

const std::string& GaleraCluster::type_string() const
{
    return type_galera;
}

const std::string& GaleraCluster::nwconf_prefix() const
{
    return my_nwconf_prefix;
}

const std::string& GaleraCluster::name() const
{
    return my_name;
}

bool GaleraCluster::reset_server(int i)
{
    auto* srv = backend(i);
    srv->stop_database();
    srv->cleanup_database();
    reset_server_settings(i);

    auto& vm = srv->vm_node();
    auto namec = vm.m_name.c_str();

    // Note: These should be done by MDBCI
    vm.run_cmd_sudo("test -d /etc/apparmor.d/ && "
                    "ln -s /etc/apparmor.d/usr.sbin.mysqld /etc/apparmor.d/disable/usr.sbin.mysqld && "
                    "sudo service apparmor restart && "
                    "chmod a+r -R /etc/my.cnf.d/*");

    bool rval = false;

    const char reset_db_cmd[] = "mariadb-install-db; sudo chown -R mysql:mysql /var/lib/mysql";
    logger().log_msgf("Running '%s' on '%s'", reset_db_cmd, namec);
    if (vm.run_cmd_sudo(reset_db_cmd) == 0)
    {
        rval = true;
    }
    else
    {
        logger().add_failure("'%s' failed on '%s'.", reset_db_cmd, namec);
    }

    // Cannot start server yet, Galera is not properly configured.
    return rval;
}

bool GaleraCluster::create_users(int i)
{
    bool rval = false;
    if (create_base_users(i))
    {
        mxt::MariaDBUserDef galmon_user = {"galeramon", "%", "galeramon"};
        galmon_user.grants = {"SUPER, REPLICATION CLIENT ON *.*"};

        auto be = backend(i);
        bool sr = supports_require();
        if (be->create_user(galmon_user, ssl_mode(), sr)
            && be->create_user(service_user_def(), ssl_mode(), sr))
        {
            rval = true;
        }
    }
    return rval;
}
