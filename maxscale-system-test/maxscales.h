#ifndef MAXSCALES_H
#define MAXSCALES_H

#include "nodes.h"
#include "mariadb_func.h"
#include "mariadb_nodes.h"

class Maxscales: public Nodes
{
public:
    Maxscales(const char *pref, const char *test_cwd, bool verbose);
    int read_env();

    /**
     * @brief rwsplit_port RWSplit service port
     */
    int rwsplit_port[256];

    /**
     * @brief readconn_master_port ReadConnection in master mode service port
     */
    int readconn_master_port[256];

    /**
     * @brief readconn_slave_port ReadConnection in slave mode service port
     */
    int readconn_slave_port[256];

    /**
     * @brief binlog_port binlog router service port
     */
    int binlog_port[256];

    /**
     * @brief conn_rwsplit  MYSQL connection struct to RWSplit service
     */
    MYSQL *conn_rwsplit[256];

    /**
     * @brief conn_master   MYSQL connection struct to ReadConnection in master mode service
     */
    MYSQL *conn_master[256];

    /**
     * @brief conn_slave MYSQL connection struct to ReadConnection in slave mode service
     */
    MYSQL *conn_slave[256];

    /**
     * @brief routers Array of 3 MYSQL handlers which contains copies of conn_rwsplit, conn_master, conn_slave
     */
    MYSQL *routers[256][3];

    /**
     * @brief ports of 3 int which contains copies of rwsplit_port, readconn_master_port, readconn_slave_port
     */
    int ports[256][3];

    /**
     * @brief maxadmin_Password Password to access Maxadmin tool
     */
    char maxadmin_password[256][256];

    /**
      * @brief maxscale_cnf full name of Maxscale configuration file
      */
    char maxscale_cnf[256][4096];

    /**
      * @brief maxscale_log_dir name of log files directory
      */
    char maxscale_log_dir[256][4096];

    /**
      * @brief maxscale_lbinog_dir name of binlog files (for binlog router) directory
      */
    char maxscale_binlog_dir[256][4096];

    /**
     * @brief N_ports Default number of routers
     */
    int N_ports[256];


    /**
    * @brief test_dir path to test application
    */
    char test_dir[4096];

    bool ssl;

    /**
     * @brief ConnectMaxscale   Opens connections to RWSplit, ReadConn master and ReadConn slave Maxscale services
     * Opens connections to RWSplit, ReadConn master and ReadConn slave Maxscale services
     * Connections stored in maxscales->conn_rwsplit[0], maxscales->conn_master[0] and maxscales->conn_slave[0] MYSQL structs
     * @return 0 in case of success
     */
    int connect_maxscale(int m);

    /**
     * @brief CloseMaxscaleConn Closes connection that were opened by ConnectMaxscale()
     * @return 0
     */
    int close_maxscale_connections(int m);

    /**
     * @brief ConnectRWSplit    Opens connections to RWSplit and store MYSQL struct in maxscales->conn_rwsplit[0]
     * @return 0 in case of success
     */
    int connect_rwsplit(int m);

    /**
     * @brief ConnectReadMaster Opens connections to ReadConn master and store MYSQL struct in maxscales->conn_master[0]
     * @return 0 in case of success
     */
    int connect_readconn_master(int m);

    /**
     * @brief ConnectReadSlave Opens connections to ReadConn slave and store MYSQL struct in maxscales->conn_slave[0]
     * @return 0 in case of success
     */
    int connect_readconn_slave(int m);

    /**
     * @brief OpenRWSplitConn   Opens new connections to RWSplit and returns MYSQL struct
     * To close connection mysql_close() have to be called
     * @return MYSQL struct
     */
    MYSQL * open_rwsplit_connection(int m)
    {
        return open_conn(rwsplit_port[m], IP[m], user_name, password, ssl);
    }

    /**
     * @brief OpenReadMasterConn    Opens new connections to ReadConn master and returns MYSQL struct
     * To close connection mysql_close() have to be called
     * @return MYSQL struct
     */
    MYSQL * open_readconn_master_connection(int m)
    {
        return open_conn(readconn_master_port[m], IP[m], user_name,
                         password, ssl);
    }

    /**
     * @brief OpenReadSlaveConn    Opens new connections to ReadConn slave and returns MYSQL struct
     * To close connection mysql_close() have to be called
     * @return  MYSQL struct
     */
    MYSQL * open_readconn_slave_connection(int m)
    {
        return open_conn(readconn_slave_port[m], IP[m], user_name,
                         password, ssl);
    }

    /**
     * @brief CloseRWSplit Closes RWplit connections stored in maxscales->conn_rwsplit[0]
     */
    void close_rwsplit(int m)
    {
        mysql_close(conn_rwsplit[m]);
        conn_rwsplit[m] = NULL;
    }

    /**
     * @brief CloseReadMaster Closes ReadConn master connections stored in maxscales->conn_master[0]
     */
    void close_readconn_master(int m)
    {
        mysql_close(conn_master[m]);
        conn_master[m] = NULL;
    }

    /**
     * @brief CloseReadSlave Closes ReadConn slave connections stored in maxscales->conn_slave[0]
     */
    void close_readconn_slave(int m)
    {
        mysql_close(conn_slave[m]);
        conn_slave[m] = NULL;
    }

    /**
     * @brief restart_maxscale Issues 'service maxscale restart' command
     */
    int restart_maxscale(int m);

    /**
     * @brief start_maxscale Issues 'service maxscale start' command
     */
    int start_maxscale(int m);

    /**
     * @brief stop_maxscale Issues 'service maxscale stop' command
     */
    int stop_maxscale(int m);

    int execute_maxadmin_command(int m, char * cmd);
    int execute_maxadmin_command_print(int m, char * cmd);
    int check_maxadmin_param(int m, const char *command, const  char *param, const  char *value);
    int get_maxadmin_param(int m, const char *command, const char *param, char *result);

    /**
     * @brief get_maxscale_memsize Gets size of the memory consumed by Maxscale process
     * @return memory size in kilobytes
     */
    long unsigned get_maxscale_memsize(int m);

    int try_query_all(int m, const char *sql);

    /**
     * @brief find_master_maxadmin Tries to find node with 'Master' status using Maxadmin connand 'show server'
     * @param nodes Mariadb_nodes object
     * @return node index if one master found, -1 if no master found or several masters found
     */
    int find_master_maxadmin(int m, Mariadb_nodes * nodes);
    int find_slave_maxadmin(int m, Mariadb_nodes * nodes);

};

#endif // MAXSCALES_H
