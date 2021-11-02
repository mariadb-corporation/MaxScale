/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-10-29
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxtest/xpand_nodes.hh>
#include <maxtest/log.hh>
#include <maxtest/mariadb_connector.hh>
#include <maxbase/format.hh>
#include <iostream>
#include <cassert>

using std::string;

namespace
{
const string type_xpand = "xpand";
const string my_nwconf_prefix = "xpand";
const string my_name = "Xpand-cluster";
}

XpandCluster::XpandCluster(mxt::SharedData* shared)
    : MariaDBCluster(shared, "xpand_server")
{
}

bool XpandCluster::reset_server(int m)
{
    // TODO: currently this function just checks the process can be started and connected with root privs.
    // To be in sync with other 'reset_server'-functions, something similar to 'mysql_install_db' is
    // required. Check other cluster types for more info.

    auto& vm = backend(m)->vm_node();
    auto name = vm.m_name.c_str();

    bool try_query = false;

    // First, check if Xpand is installed. If not, fail immediately.
    string status_cmd = "systemctl status clustrix";
    auto res_status = vm.run_cmd_output_sudo(status_cmd);
    // "status" returns 0 for running, 1-3 for stopped, 4 for unknown.
    if (res_status.rc == 0)
    {
        try_query = true;
    }
    else if (res_status.rc >= 1 && res_status.rc <= 3)
    {
        logger().log_msgf("Xpand installed but not running on '%s'. Trying to start.", name);
        string start_cmd = "systemctl start clustrix";
        auto res = vm.run_cmd_output_sudo(start_cmd);
        if (res.rc == 0)
        {
            try_query = true;
        }
        else
        {
            logger().log_msgf("Xpand start failed on '%s'. Command '%s' returned %i: '%s'",
                              name, start_cmd.c_str(), res.rc, res.output.c_str());
        }
    }
    else
    {
        logger().log_msgf("Xpand is not installed on '%s'. Command '%s' returned %i: '%s'",
                          name, status_cmd.c_str(), res_status.rc, res_status.output.c_str());
    }

    bool running = false;
    if (try_query)
    {
        logger().log_msgf("Xpand running on '%s'. Testing a simple query as root...", name);

        auto query_test = [&vm]() {
                return vm.run_cmd_sudo("mysql -e 'SELECT @@server_id;'");
            };

        if (query_test() == 0)
        {
            running = true;
        }
        else
        {
            int i = 0;
            while (i < 6)
            {
                logger().log_msgf("Query test failed on '%s'. Restarting, waiting a bit and retrying.", name);
                string restart_cmd = "systemctl restart clustrix";
                auto res = vm.run_cmd_output_sudo(restart_cmd);
                sleep(5);
                if (res.rc == 0)
                {
                    if (query_test() == 0)
                    {
                        running = true;
                        break;
                    }
                }
                else
                {
                    logger().log_msgf("Xpand restart failed on '%s'. Command '%s' returned %i: '%s'",
                                      name, restart_cmd.c_str(), res.rc, res.output.c_str());
                }
                i++;
            }
        }
    }

    return running;
}

bool XpandCluster::start_replication()
{
    const int n = N;
    // Generate base users on every node, form cluster, generate rest of users on just one node.
    auto func = [this](int node) {
            return create_base_users(node);
        };

    bool rval = false;
    if (run_on_every_backend(func))
    {
        // Check that admin user can connect too all nodes.
        if (update_status())
        {
            // The nodes must be added one by one to the cluster. An attempt to add them
            // all with one ALTER command will fail, if one or more of them already are in
            // the cluster.
            int failed_nodes = 0;
            auto conn = backend(0)->admin_connection();

            for (int new_node = 1; new_node < n; new_node++)
            {
                string add_cmd = mxb::string_printf("ALTER CLUSTER ADD '%s';", ip_private(new_node));
                bool node_added = false;
                int attempts = 0;
                while (!node_added)
                {
                    if (conn->try_cmd(add_cmd))
                    {
                        node_added = true;
                    }
                    else
                    {
                        string error = conn->error();
                        if (error.find("already in cluster") != string::npos)
                        {
                            // E.g. '[25609] Bad parameter.: Host "10.166.0.171" already in cluster'
                            // That's ok and can be ignored.
                            node_added = true;
                        }
                        else if (error.find("addition is pending") != string::npos
                                 || error.find("group change in progress") != string::npos)
                        {
                            // E.g. '[50180] Multiple nodes cannot be added when an existing addition is
                            // pending'
                            // E.g. '[16388] Group change during GTM operation: group change in progress,
                            //       try restarting transaction'
                            // Sleep and retry.

                            if (attempts < 5)
                            {
                                int sleep_s = attempts + 2;
                                logger().log_msgf("Received error '%s' when adding '%s' to %s. "
                                                  "Retrying after %d seconds.",
                                                  error.c_str(), node(new_node)->m_name.c_str(),
                                                  name().c_str(), sleep_s);
                                sleep(sleep_s);
                            }
                            else
                            {
                                logger().log_msgf("After %d attempts, still could not add '%s' to %s, "
                                                  "bailing out.",
                                                  attempts, node(new_node)->m_name.c_str(),
                                                  name().c_str());
                                break;
                            }
                        }
                        else
                        {
                            logger().log_msgf("Fatal error when adding '%s' to %s: %s",
                                              node(new_node)->m_name.c_str(),
                                              name().c_str(), error.c_str());
                            break;
                        }
                    }
                    attempts++;
                }

                if (!node_added)
                {
                    failed_nodes++;
                }
            }

            if (failed_nodes == 0)
            {
                // At this point, the last cluster add operation may still be going on, and writes will fail.
                // Sleep for a while to let it complete. There is probably some better way to do this...
                sleep(5);
                logger().log_msgf("All nodes added to %s. Generating some more users...", name().c_str());

                // Generate the rest of the users.
                if (create_xpand_users(0))
                {
                    rval = true;
                }
                else
                {
                    // Perhaps group change is still going on. Wait some more and try again.
                    logger().log_msgf("User generation failed, maybe group change is still going on. "
                                      "Retry after 10s.");
                    sleep(10);
                    if (create_xpand_users(0))
                    {
                        rval = true;
                    }
                    else
                    {
                        logger().log_msgf("User generation failed again, %s must be broken.", name().c_str());
                    }
                }
            }
        }
        else
        {
            logger().log_msgf("Failed to query status of %s.", name().c_str());
        }
    }
    else
    {
        logger().log_msgf("Failed to generate base users on %s.", name().c_str());
    }

    return rval;
}

bool XpandCluster::check_replication()
{
    bool res = true;
    if (connect() == 0)
    {
        for (int i = 0; i < N; i++)
        {
            int n = execute_query_count_rows(nodes[i], "select * from system.nodeinfo");

            if (n != N)
            {
                printf("Expected %d nodes configured at node %d, found %d", N, i, n);
                res = false;
            }
        }
    }
    else
    {
        res = false;
    }

    close_connections();    // Some might have been created by connect().

    return res;
}

std::string XpandCluster::block_command(int node) const
{
    std::string command = MariaDBCluster::block_command(node);

    // Block health-check port as well.
    command += ";";
    command += "iptables -I INPUT -p tcp --dport 3581 -j REJECT";
    command += ";";
    command += "ip6tables -I INPUT -p tcp --dport 3581 -j REJECT";

    return command;
}

std::string XpandCluster::unblock_command(int node) const
{
    std::string command = MariaDBCluster::unblock_command(node);

    // Unblock health-check port as well.
    command += ";";
    command += "iptables -I INPUT -p tcp --dport 3581 -j ACCEPT";
    command += ";";
    command += "ip6tables -I INPUT -p tcp --dport 3581 -j ACCEPT";

    return command;
}

const std::string& XpandCluster::type_string() const
{
    return type_xpand;
}

std::string XpandCluster::anonymous_users_query() const
{
    return "SELECT CONCAT('\\'', user, '\\'@\\'', host, '\\'') FROM system.users WHERE user = ''";
}

const std::string& XpandCluster::nwconf_prefix() const
{
    return my_nwconf_prefix;
}

const std::string& XpandCluster::name() const
{
    return my_name;
}

std::string XpandCluster::get_srv_cnf_filename(int node)
{
    std::cout << "Error: Server configuration file not specified for Xpand." << std::endl;
    assert(!true);
    return "";
}

bool XpandCluster::create_users(int i)
{
    return create_base_users(i) && create_xpand_users(i);
}

bool XpandCluster::create_xpand_users(int node)
{
    bool rval = false;
    mxt::MariaDBUserDef xpmon_user = {"xpandmon", "%", "xpandmon"};
    xpmon_user.grants = {"SELECT ON system.membership",       "SELECT ON system.nodeinfo",
                         "SELECT ON system.softfailed_nodes", "SUPER ON *.*"};

    // Xpand service-user requires special grants.
    mxt::MariaDBUserDef service_user = {"maxservice", "%", "maxservice"};
    service_user.grants = {"SELECT ON system.users", "SELECT ON system.user_acl"};

    auto be = backend(node);
    if (be->create_user(xpmon_user, ssl_mode()) && be->create_user(service_user, ssl_mode()))
    {
        rval = true;
    }
    return rval;
}
