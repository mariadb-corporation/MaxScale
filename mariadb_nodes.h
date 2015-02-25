#ifndef MARIADB_NODES_H
#define MARIADB_NODES_H

/**
 * @file mariadb_nodes.h - backend nodes routines
 *
 * @verbatim
 * Revision History
 *
 * Date		Who		Description
 * 17/11/14	Timofey Turenko	Initial implementation
 *
 * @endverbatim
 */


#include "mariadb_func.h"

/**
 * @brief A class to handle backend nodes
 * Contains references up to 256 nodes, info about IP, port, ssh key, use name and password for each node
 * Node parameters should be defined in the enviromental variables in the follwing way:
 * prefix_N - N number of nodes in the setup
 * prefix_NNN - IP adress of the node (NNN 3 digits node index)
 * prefix_port_NNN - MariaDB port number of the node
 * prefix_User - User name to access backend setup (should have full access to 'test' DB with GRANT OPTION)
 * prefix_Password - Password to access backend setup
 */
class Mariadb_nodes
{
public:
    /**
     * @brief Constructor
     * @param pref  name of backend setup (like 'repl' or 'galera')
     */
    Mariadb_nodes(char * pref);
    /**
    * @brief  MYSQL structs for every backend node
    */
    MYSQL *nodes[256];
    /**
     * @brief  IP address strings for every backend node
     */
    char IP[256][16];
    /**
     * @brief  MariaDB port for every backend node
     */
    int Ports[256];
    /**
     * @brief  Path to ssh key for every backend node
     */
    char sshkey[256][4096];
    /**
     * @brief Number of backend nodes
     */
    int N;
    /**
     * @brief   User name to access backend nodes
     */
    char User[256];
    /**
     * @brief   Password to access backend nodes
     */
    char Password[256];
    int Master;
    /**
     * @brief     name of backend setup (like 'repl' or 'galera')
     */
    char prefix[16];
    /**
    * @brief  Opens connctions to all backend nodes (to 'test' DB)
    * @return 0 in case of success
    */
    int Connect();
    /**
     * @brief  close connections which were previously opened by Connect()
     * @return
     */
    int CloseConn();
    /**
     * @brief reads IP, Ports, sshkeys for every node from enviromental variables as well as number of nodes (N) and  User/Password
     * @return 0
     */
    int ReadEnv();
    /**
     * @brief  prints all nodes information
     * @return 0
     */
    int PrintIP();

    int FindMaster();
    /**
     * @brief ChangeMaster  set a new master node for Master/Slave setup
     * @param NewMaster index of new Master node
     * @param OldMaster index of current Master node
     * @return  0 in case of success
     */
    int ChangeMaster(int NewMaster, int OldMaster);


    /**
     * @brief StopNodes stops mysqld on all nodes
     * @return  0 in case of success
     */
    int StopNodes();

    /**
     * @brief StartReplication configure nodes and start Master/Slave replication
     * @return  0 in case of success
     */
    int StartReplication();

    /**
     * @brief StartBinlog configure first node as Master, Second as slave connected to Master and others as slave connected to MaxScale binlog router
     * @param MaxScale_IP IP of MaxScale machine
     * @param Binlog_Port port of binlog router listener
     * @return  0 in case of success
     */
    int StartBinlog(char * Maxscale_IP, int Binlog_Port);

    /**
     * @brif BlockNode setup firewall on a backend node to block MariaDB port
     * @param node Index of node to block
     * @return 0 in case of success
     */
    int BlockNode(int node);

    /**
     * @brif UnblockNode setup firewall on a backend node to unblock MariaDB port
     * @param node Index of node to unblock
     * @return 0 in case of success
     */
    int UnblockNode(int node);
};

#endif // MARIADB_NODES_H
