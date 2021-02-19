/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-02-16
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <fstream>
#include <iostream>
#include <regex>
#include <maxtest/xpand_nodes.hh>

using std::string;

namespace
{
const string type_xpand = "xpand";
}

int XpandCluster::prepare_server(int m)
{
    int rv = 1;
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
            rv = 0;
        }
        else
        {
            printf("Cannot access Xpand using user '%s', creating users.\n", this->user_name.c_str());
            // TODO: We need an return code here.
            create_users(m);
            rv = 0;
        }
    }

    return rv;
}


int XpandCluster::start_replication()
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
                    else if (error.find("addition is pending") != std::string::npos)
                    {
                        // E.g. '[50180] Multiple nodes cannot be added when an existing addition is pending'
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

    return rv;
}

std::string XpandCluster::cnf_servers()
{
    std::string s;
    for (int i = 0; i < N; i++)
    {
        s += std::string("\\n[")
                + cnf_server_name
                + std::to_string(i + 1)
                + std::string("]\\ntype=server\\naddress=")
                + std::string(ip_private(i))
                + std::string("\\nport=")
                + std::to_string(port[i])
                + std::string("\\nprotocol=MySQLBackend\\n");
    }
    return s;
}

int XpandCluster::check_replication()
{
    int res = 0;
    if (connect() == 0)
    {
        for (int i = 0; i < N; i++)
        {
            int n = execute_query_count_rows(nodes[i], "select * from system.nodeinfo");

            if (n != N)
            {
                printf("Expected %d nodes configured at node %d, found %d", N, i, n);
                res = 1;
            }
        }
    }
    else
    {
        res = 1;
    }

    close_connections(); // Some might have been created by connect().

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

bool XpandCluster::setup()
{
    return MariaDBCluster::setup();
}

const std::string& XpandCluster::type_string() const
{
    return type_xpand;
}

std::string XpandCluster::anonymous_users_query() const
{
    return "SELECT CONCAT('\\'', user, '\\'@\\'', host, '\\'') FROM system.users WHERE user = ''";
}
