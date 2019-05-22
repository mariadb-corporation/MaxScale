#include <fstream>
#include <iostream>
#include <sstream>
#include "clustrix_nodes.h"

int Clustrix_nodes::prepare_server(int m)
{
    int ec;
    char* clustrix_rpm = ssh_node_output(m, "rpm -qa | grep clustrix-clxnode", true, &ec);
    if (strstr(clustrix_rpm, "clustrix-clxnode") == NULL)
    {
        printf("%s\n", ssh_node_output(m, CLUSTRIX_DEPS_YUM, true, &ec));
        printf("%s\n", ssh_node_output(m, WGET_CLUSTRIX, false, &ec));
        printf("%s\n", ssh_node_output(m, UNPACK_CLUSTRIX, false, &ec));
        printf("%s\n", ssh_node_output(m, INSTALL_CLUSTRIX, false, &ec));
        create_users(m);
    }
    else
    {
        printf("%s\n", ssh_node_output(m, "systemctl restart clustrix", true, &ec));
    }

    return 0;
}

int Clustrix_nodes::start_replication()
{
    for (int i = 0; i < N; i++)
    {
        prepare_server(i);
    }
    std::string lic_filename = std::string(getenv("HOME"))
            + std::string("/.config/mdbci/clustrix_license");
    std::ifstream lic_file;
    lic_file.open(lic_filename.c_str());
    std::stringstream strStream;
    strStream << lic_file.rdbuf();
    std::string clustrix_license = strStream.str();
    lic_file.close();

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
    return 0;
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
    connect();
    for (int i = 0; i < N; i++)
    {
        if (execute_query_count_rows(nodes[i], "select * from system.nodeinfo") != N)
        {
            res = 1;
        }
    }
    close_connections();

    return res;
}

string Clustrix_nodes::block_command(int node) const
{
    string command = Mariadb_nodes::block_command(node);

    // Block health-check port as well.
    command += ";";
    command += "iptables -I INPUT -p tcp --dport 3581 -j REJECT";
    command += ";";
    command += "ip6tables -I INPUT -p tcp --dport 3581 -j REJECT";

    return command;
}

string Clustrix_nodes::unblock_command(int node) const
{
    string command = Mariadb_nodes::unblock_command(node);

    // Unblock health-check port as well.
    command += ";";
    command += "iptables -I INPUT -p tcp --dport 3581 -j ACCEPT";
    command += ";";
    command += "ip6tables -I INPUT -p tcp --dport 3581 -j ACCEPT";

    return command;
}
