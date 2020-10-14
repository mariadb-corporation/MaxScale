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

#include <fstream>
#include <iostream>
#include <regex>
#include <maxtest/clustrix_nodes.hh>

int Clustrix_nodes::prepare_server(int m)
{
    int rv = 1;
    int ec;
    char* clustrix_rpm = ssh_node_output(m, "rpm -qa | grep clustrix-clxnode", true, &ec);
    if (strstr(clustrix_rpm, "clustrix-clxnode") == NULL)
    {
        char* str1 = nullptr;
        char* str2 = nullptr;
        char* str3 = nullptr;
        char* str4 = nullptr;

        str1 = ssh_node_output(m, CLUSTRIX_DEPS_YUM, true, &ec);
        if (ec == 0)
        {
            printf("Installed clustrix dependencies on node %d.\n", m);
            str2 = ssh_node_output(m, WGET_CLUSTRIX, false, &ec);
            if (ec == 0)
            {
                printf("Wgot Clustrix installation package on node %d.\n", m);
                str3 = ssh_node_output(m, UNPACK_CLUSTRIX, false, &ec);
                if (ec == 0)
                {
                    printf("Unpacked Clustrix package on node %d.\n", m);
                    str4 = ssh_node_output(m, INSTALL_CLUSTRIX, false, &ec);
                    if (ec == 0)
                    {
                        printf("Successfully installed Clustrix on node %d.\n", m);
                    }
                    else
                    {
                        printf("Error: Could not install Clustrix package on node %d: %s\n", m, str4);
                    }
                }
                else
                {
                    printf("Error: Could not unpack Clustrix package on node %d: %s\n", m, str3);
                }
            }
            else
            {
                printf("Error: Could not wget Clustrix installation package on node %d: %s\n", m, str2);
            }
        }
        else
        {
            printf("Error: Could not install Clustrix dependencies on node %d: %s\n", m, str1);
        }

        free(str4);
        free(str3);
        free(str2);
        free(str1);
    }

    free(clustrix_rpm);

    bool running = false;

    ec = ssh_node(m, "systemctl status clustrix", true);

    if (ec == 0)
    {
        printf("Clustrix running on node %d.\n", m);

        ec = ssh_node(m, "mysql -e 'SELECT @@server_id'", true);
        if (ec == 0)
        {
            running = true;
        }
        else
        {
            printf("Could not connect as root to Clustrix on node %d, restarting.\n", m);

            ec = ssh_node(m, "systemctl restart clustrix", true);

            if (ec == 0)
            {
                printf("Successfully restarted Clustrix on node %d.\n", m);
                running = true;
            }
            else
            {
                printf("Could not restart Clustrix on node %d.\n", m);
            }
        }
    }
    else
    {
        printf("Clustrix not running on node %d, starting.\n", m);

        ec = ssh_node(m, "systemctl start clustrix", true);

        if (ec == 0)
        {
            printf("Successfully started Clustrix on node %d.\n", m);
            running = true;
        }
        else
        {
            printf("Could not start Clustrix on node %d.\n", m);
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
                printf("Could not connect to Clustrix as root on node %d, "
                       "sleeping a while (totally at most ~1 minute) and retrying.\n", m);
                sleep(10);
            }
        }
        while (ec != 0 && now - start < 60);

        if (ec == 0)
        {
            printf("Could connect as root to Clustrix on node %d.\n", m);
            check_users = true;
        }
        else
        {
            printf("Could not connect as root to Clustrix on node %d within given timeframe.\n", m);
        }
    }

    if (check_users)
    {
        std::string command("mysql ");
        command += "-u ";
        command += this->user_name;
        command += " ";
        command += "-p";
        command += this->password;

        ec = ssh_node(m, command.c_str(), false);

        if (ec == 0)
        {
            printf("Can access Clustrix using user '%s'.\n", this->user_name);
            rv = 0;
        }
        else
        {
            printf("Cannot access Clustrix using user '%s', creating users.\n", this->user_name);
            // TODO: We need an return code here.
            create_users(m);
            rv = 0;
        }
    }

    return rv;
}

namespace
{

using std::string;

bool license_is_valid(const std::string& license)
{
    static const std::regex regex("\"expiration\":\"[^\"]+\"");

    bool is_valid = false;

    std::smatch match;

    if (std::regex_search(license, match, regex))
    {
        if (match.size() == 1)
        {
            string s = match[0].str();

            s = s.substr(14, 10); // We expect something like '"expiration":"2019-08-21 00:00:00"'

            if (s.length() == 10) // '2019-08-21' (excluding quotes)
            {
                int year = atoi(s.substr(0, 4).c_str());  // 2019
                int month = atoi(s.substr(5, 2).c_str()); // 08
                int day = atoi(s.substr(8, 2).c_str());   // 21

                time_t timestamp = time(NULL);
                struct tm now;
                localtime_r(&timestamp, &now);

                now.tm_year += 1900;
                now.tm_mon += 1;

                if (year >= now.tm_year
                    && (month > now.tm_mon || (month == now.tm_mon && day >= now.tm_mday)))
                {
                    is_valid = true;
                }
                else
                {
                    printf("ERROR: The date is %d-%d-%d, but the license in the license file "
                           "is valid only until %d-%d-%d.\n",
                           now.tm_year, now.tm_mon, now.tm_mday,
                           year, month, day);
                }
            }
            else
            {
                printf("ERROR: The value of the key 'expiration' does not appear to be valid.\n");
            }
        }
        else
        {
            printf("ERROR: The license in the license file either does not contain an "
                   "'expiration' key or then it contains several.\n");
        }
    }
    else
    {
        printf("ERROR: The license file does not seem to contain a valid license.\n");
    }

    return is_valid;
}

}

int Clustrix_nodes::start_replication()
{
    int rv = 1;

    std::string lic_filename = std::string(getenv("HOME"))
            + std::string("/.config/mdbci/clustrix_license");
    std::ifstream lic_file;
    lic_file.open(lic_filename.c_str());

    if (lic_file.is_open())
    {
        printf("Using license file '%s'.\n", lic_filename.c_str());

        std::stringstream ss;
        ss << lic_file.rdbuf();
        std::string clustrix_license = ss.str();
        lic_file.close();

        if (license_is_valid(clustrix_license))
        {
            execute_query_all_nodes(clustrix_license.c_str());

            std::string cluster_setup_sql = std::string("ALTER CLUSTER ADD '")
                + std::string(IP_private[1])
                + std::string("'");
            for (int i = 2; i < N; i++)
            {
                cluster_setup_sql += std::string(",'")
                    + std::string(IP_private[i])
                    + std::string("'");
            }
            connect();
            execute_query(nodes[0], "%s", cluster_setup_sql.c_str());
            close_connections();

            rv = 0;
        }
    }
    else
    {
        printf("ERROR: The Clustrix license file '%s' does not exist. "
               "It must contain a string \"set global license='{...}';\" using which the "
               "Clustrix license can be set.\n",
               lic_filename.c_str());
    }

    return rv;
}

std::string Clustrix_nodes::cnf_servers()
{
    std::string s;
    for (int i = 0; i < N; i++)
    {
        s += std::string("\\n[")
                + cnf_server_name
                + std::to_string(i + 1)
                + std::string("]\\ntype=server\\naddress=")
                + std::string(IP_private[i])
                + std::string("\\nport=")
                + std::to_string(port[i])
                + std::string("\\nprotocol=MySQLBackend\\n");
    }
    return s;
}

int Clustrix_nodes::check_replication()
{
    int res = 0;
    if (connect() == 0)
    {
        for (int i = 0; i < N; i++)
        {
            if (execute_query_count_rows(nodes[i], "select * from system.nodeinfo") != N)
            {
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

std::string Clustrix_nodes::block_command(int node) const
{
    std::string command = Mariadb_nodes::block_command(node);

    // Block health-check port as well.
    command += ";";
    command += "iptables -I INPUT -p tcp --dport 3581 -j REJECT";
    command += ";";
    command += "ip6tables -I INPUT -p tcp --dport 3581 -j REJECT";

    return command;
}

std::string Clustrix_nodes::unblock_command(int node) const
{
    std::string command = Mariadb_nodes::unblock_command(node);

    // Unblock health-check port as well.
    command += ";";
    command += "iptables -I INPUT -p tcp --dport 3581 -j ACCEPT";
    command += ";";
    command += "ip6tables -I INPUT -p tcp --dport 3581 -j ACCEPT";

    return command;
}
