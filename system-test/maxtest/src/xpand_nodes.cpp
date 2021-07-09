/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-07-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxtest/xpand_nodes.hh>
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
    bool rv = false;
    int ec;

    bool running = false;

    ec = ssh_node(m, "systemctl status clustrix", true);

    if (ec == 0)
    {
        printf("Xpand running on node %d.\n", m);

        ec = ssh_node(m, "mysql -e 'SELECT @@server_id'", true);
        if (ec == 0)
        {
            running = true;
        }
        else
        {
            printf("Could not connect as root to Xpand on node %d, restarting.\n", m);

            ec = ssh_node(m, "systemctl restart clustrix", true);

            if (ec == 0)
            {
                printf("Successfully restarted Xpand on node %d.\n", m);
                running = true;
            }
            else
            {
                printf("Could not restart Xpand on node %d.\n", m);
            }
        }
    }
    else
    {
        printf("Xpand not running on node %d, starting.\n", m);

        ec = ssh_node(m, "systemctl start clustrix", true);

        if (ec == 0)
        {
            printf("Successfully started Xpand on node %d.\n", m);
            running = true;
        }
        else
        {
            printf("Could not start Xpand on node %d.\n", m);
        }
    }

    bool check_users = false;

    if (running)
    {
        int start = time(NULL);
        int now;

        do
        {
            ec = ssh_node(m, "mysql -e 'SELECT @@server_id'", true);
            now = time(NULL);

            if (ec != 0)
            {
                printf("Could not connect to Xpand as root on node %d, "
                       "sleeping a while (totally at most ~1 minute) and retrying.\n", m);
                sleep(10);
            }
        }
        while (ec != 0 && now - start < 60);

        if (ec == 0)
        {
            printf("Could connect as root to Xpand on node %d.\n", m);
            check_users = true;
        }
        else
        {
            printf("Could not connect as root to Xpand on node %d within given timeframe.\n", m);
        }
    }

    if (check_users)
    {
        std::string command("mysql ");
        command += "-u ";
        command += "xpandmon";
        command += " ";
        command += "-p";
        command += "xpandmon";

        ec = ssh_node(m, command.c_str(), false);

        if (ec == 0)
        {
            printf("Can access Xpand using user '%s'.\n", this->user_name.c_str());
            rv = true;
        }
        else
        {
            printf("Cannot access Xpand using user '%s', creating users.\n", this->user_name.c_str());
            // TODO: We need an return code here.
            create_users(m);
            rv = true;
        }
    }

    return rv;
}

/**
 * @brief start_cluster Intstalls Xpand on all nodes, configure license, form cluster
 * @return True on success
 */
bool XpandCluster::start_replication()
{
    int rv = 0;

    if (connect() == 0)
    {
        // The nodes must be added one by one to the cluster. An attempt to add them
        // all with one ALTER command will fail, if one or more of them already are in
        // the cluster.

        for (int i = 1; i < N; ++i)
        {
            std::string cluster_setup_sql = std::string("ALTER CLUSTER ADD '")
                + std::string(ip_private(i))
                + std::string("'");

            bool retry = false;
            int attempts = 0;

            do
            {
                ++attempts;

                rv = execute_query(nodes[0], "%s", cluster_setup_sql.c_str());

                if (rv != 0)
                {
                    std::string error(mysql_error(nodes[0]));

                    if (error.find("already in cluster") != std::string::npos)
                    {
                        // E.g. '[25609] Bad parameter.: Host "10.166.0.171" already in cluster'
                        // That's ok and can be ignored.
                        rv = 0;
                    }
                    else if (error.find("addition is pending") != std::string::npos
                             || error.find("group change in progress") != std::string::npos)
                    {
                        // E.g. '[50180] Multiple nodes cannot be added when an existing addition is pending'
                        // E.g. '[16388] Group change during GTM operation: group change in progress,
                        //       try restarting transaction'
                        // Sleep and retry.

                        if (attempts < 5)
                        {
                            printf("Retrying after %d seconds.", attempts);
                            sleep(attempts);
                            retry = true;
                        }
                        else
                        {
                            printf("After %d attempts, still could not add node to cluster, bailing out.",
                                   attempts);
                            retry = false;
                        }
                    }
                    else
                    {
                        printf("Fatal error when setting up xpand: %s", error.c_str());
                        retry = false;
                    }
                }
            }
            while (rv != 0 && retry);

            if (rv != 0)
            {
                break;
            }
        }

        close_connections();
    }
    else
    {
        rv = 1;
    }

    return rv == 0;
}

bool XpandCluster::check_normal_conns()
{
    return MariaDBCluster::check_normal_conns() && check_conns("xpandmon", "xpandmon");
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
    bool rval = false;
    if (create_base_users(i))
    {
        mxt::MariaDBUserDef xpmon_user = {"xpandmon", "%", "xpandmon"};
        xpmon_user.grants = {"SELECT ON system.membership",       "SELECT ON system.nodeinfo",
                             "SELECT ON system.softfailed_nodes", "SUPER ON *.*"};

        mxt::MariaDBUserDef service_user = {"maxservice", "%", "maxservice"};
        service_user.grants = {"SELECT ON system.users", "SELECT ON system.user_acl"};

        auto be = backend(i);
        bool sr = supports_require();
        if (be->create_user(xpmon_user, ssl_mode(), sr) && be->create_user(service_user, ssl_mode(), sr))
        {
            rval = true;
        }
    }
    return rval;
}
