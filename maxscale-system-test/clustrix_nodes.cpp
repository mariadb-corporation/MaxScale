#include <fstream>
#include <iostream>
#include <sstream>
#include "clustrix_nodes.h"

int Clustrix_nodes::install_clustrix(int m)
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
    return 0;
}

int Clustrix_nodes::start_cluster()
{
    for (int i = 0; i < N; i++)
    {
        install_clustrix(i);
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
