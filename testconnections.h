#ifndef TESTCONNECTIONS_H
#define TESTCONNECTIONS_H

#include "mariadb_nodes.h"

/**
 * @brief Class contains references to Master/Slave and Galera test setups
 * Test setup should consist of two setups: one Master/Slave and one Galera.
 *
 * Maxscale should be configured separatelly for every test.
 *
 * Test setup should be described by enviromental variables:
 * - Maxscale_IP - IP adress of Maxscale machine
 * - Maxscale_User - User name to access Maxscale services
 * - Maxscale_Password - Password to access Maxscale services
 * - Maxscale_sshkey - ssh key for Maxscale machine
 * - maxdir - Path to Maxscale home direcdtory
 * - KillVMCommand - Command to kill a node (should handle one parameter: IP address of virtual machine to kill)
 * - StartVMCommand - Command to restart virtual machine (should handle one parameter: IP address of virtual machine to kill)
 * - GetLogsCommand - Command to copy log files from node virtual machines (should handle one parameter: IP address of virtual machine to kill)
 * - SysbenchDir - path to SysBench directory (sysbanch should be >= 0.5)
 * - repl_N - Number of Master/Slave setup nodes
 * - repl_NNN - IP address of node NNN (NNN - 3 digits node index starting from 000)
 * - repl_port_NNN - MariaDB port for node NNN
 * - repl_sshkey_NNN - ssh key to access node NNN (should be sutable for 'root' and 'ec2-user')
 * - repl_User - User name to access Master/Slav setup
 * - repl_Password - Password to access Master/Slave setup
 * - galera_N, galera_NNN, galera_port_NNN, galera_sshkey_NNN, galera_User, galera_Password - same for Galera setup
 *
 */
class TestConnections
{
public:
    /**
     * @brief TestConnections constructor
     */
    TestConnections();

    /**
     * @brief rwsplit_port RWSplit service port
     */
    int rwsplit_port;

    /**
     * @brief readconn_master_port ReadConnection in master mode service port
     */
    int readconn_master_port;

    /**
     * @brief readconn_slave_port ReadConnection in slave mode service port
     */
    int readconn_slave_port;

    /**
     * @brief conn_rwsplit  MYSQL connection struct to RWSplit service
     */
    MYSQL *conn_rwsplit;

    /**
     * @brief conn_master   MYSQL connection struct to ReadConnection in master mode service
     */
    MYSQL *conn_master;

    /**
     * @brief conn_slave MYSQL connection struct to ReadConnection in slave mode service
     */
    MYSQL *conn_slave;

    /**
     * @brief galera Mariadb_nodes object containing references to Galera setuo
     */
    Mariadb_nodes * galera;

    /**
     * @brief repl Mariadb_nodes object containing references to Master/Slave setuo
     */
    Mariadb_nodes * repl;

    /**
     * @brief Maxscale_IP   Maxscale machine IP address
     */
    char Maxscale_IP[16];

    /**
     * @brief Maxscale_User User name to access Maxscale services
     */
    char Maxscale_User[256];

    /**
     * @brief Maxscale_Password Password to access Maxscale services
     */
    char Maxscale_Password[256];

    /**
     * @brief Maxscale_sshkey   ssh key for Maxscale machine
     */
    char Maxscale_sshkey[4096];

    /**
     * @brief KillVMCommand Command to kill a node (should handle one parameter: IP address of virtual machine to kill)
     */
    char KillVMCommand[4096];

    /**
     * @brief GetLogsCommand    Command to copy log files from node virtual machines (should handle one parameter: IP address of virtual machine to kill)
     */
    char GetLogsCommand[4096];

    /**
     * @brief StartVMCommand    Command to restart virtual machine (should handle one parameter: IP address of virtual machine to kill)
     */
    char StartVMCommand[4096];

    /**
     * @brief SysbenchDir   path to SysBench directory (sysbanch should be >= 0.5)
     */
    char SysbenchDir[4096];

    /**
     * @brief ReadEnv Reads all Maxscale and Master/Slave and Galera setups info from environmental variables
     * @return 0 in case of success
     */
    int ReadEnv();

    /**
     * @brief PrintIP   Prints all Maxscale and Master/Slave and Galera setups info
     * @return 0
     */
    int PrintIP();

    /**
     * @brief ConnectMaxscale   Opens connections to RWSplit, ReadConn master and ReadConn slave Maxscale services
     * Opens connections to RWSplit, ReadConn master and ReadConn slave Maxscale services
     * Connections stored in conn_rwsplit, conn_master and conn_slave MYSQL structs
     * @return 0 in case of success
     */
    int ConnectMaxscale();

    /**
     * @brief CloseMaxscaleConn Closes connection that were opened by ConnectMaxscale()
     * @return 0
     */
    int CloseMaxscaleConn();

    /**
     * @brief ConnectRWSplit    Opens connections to RWSplit and store MYSQL struct in conn_rwsplit
     * @return 0 in case of success
     */
    int ConnectRWSplit() {conn_rwsplit = open_conn(rwsplit_port, Maxscale_IP, Maxscale_User, Maxscale_Password); if (conn_rwsplit == NULL){return(1);} else {return(0);}}

    /**
     * @brief ConnectReadMaster Opens connections to ReadConn master and store MYSQL struct in conn_master
     * @return 0 in case of success
     */
    int ConnectReadMaster() {conn_master = open_conn(readconn_master_port, Maxscale_IP, Maxscale_User, Maxscale_Password);  if (conn_master == NULL){return(1);} else {return(0);}}

    /**
     * @brief ConnectReadSlave Opens connections to ReadConn slave and store MYSQL struct in conn_slave
     * @return 0 in case of success
     */
    int ConnectReadSlave() {conn_slave = open_conn(readconn_slave_port, Maxscale_IP, Maxscale_User, Maxscale_Password); if (conn_slave == NULL){return(1);} else {return(0);}}

    /**
     * @brief OpenRWSplitConn   Opens new connections to RWSplit and returns MYSQL struct
     * To close connection mysql_close() have to be called
     * @return MYSQL struct
     */
    MYSQL * OpenRWSplitConn() {return open_conn(rwsplit_port, Maxscale_IP, Maxscale_User, Maxscale_Password);}

    /**
     * @brief OpenReadMasterConn    Opens new connections to ReadConn master and returns MYSQL struct
     * To close connection mysql_close() have to be called
     * @return MYSQL struct
     */
    MYSQL * OpenReadMasterConn() {return open_conn(readconn_master_port, Maxscale_IP, Maxscale_User, Maxscale_Password);}

    /**
     * @brief OpenReadSlaveConn    Opens new connections to ReadConn slave and returns MYSQL struct
     * To close connection mysql_close() have to be called
     * @return  MYSQL struct
     */
    MYSQL * OpenReadSlaveConn() {return open_conn(readconn_slave_port, Maxscale_IP, Maxscale_User, Maxscale_Password);}

    /**
     * @brief CloseRWSplit Closes RWplit connections stored in conn_rwsplit
     */
    void CloseRWSplit(){mysql_close(conn_rwsplit);}

    /**
     * @brief CloseReadMaster Closes ReadConn master connections stored in conn_master
     */
    void CloseReadMaster(){mysql_close(conn_master);}
    /**
     * @brief CloseReadSlave Closes ReadConn slave connections stored in conn_slave
     */
    void CloseReadSlave(){mysql_close(conn_slave);}


};

/**
 * @brief CheckLogErr Reads error log and tried to search for given string
 * @param err_msg Error message to search in the log
 * @param expected TRUE if err_msg is expedted in the log, FALSE if err_msg should NOT be in the log
 * @return 0 if (err_msg is found AND expected is TRUE) OR (err_msg is NOT found in the log AND expected is FALSE)
 */
int CheckLogErr(char * err_msg, bool expected);

/**
 * @brief FindConnectedSlave Finds slave node which has connections from MaxScale
 * @param Test TestConnections object which contains info about test setup
 * @param global_result pointer to variable which is increased in case of error
 * @return index of found slave node
 */
int FindConnectedSlave(TestConnections* Test, int * global_result);

/**
 * @brief FindConnectedSlave1 same as FindConnectedSlave() but does not increase global_result
 * @param Test  TestConnections object which contains info about test setup
 * @return index of found slave node
 */
int FindConnectedSlave1(TestConnections* Test);

/**
 * @brief CheckMaxscaleAlive Checks if MaxScale is alive
 * Reads test setup info from enviromental variables and tries to connect to all Maxscale services to check if i is alive.
 * Also 'show processlist' query is executed using all services
 * @return 0 in case if success
 */
int CheckMaxscaleAlive();

#endif // TESTCONNECTIONS_H
