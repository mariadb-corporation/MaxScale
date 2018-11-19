#ifndef MAXSCALES_H
#define MAXSCALES_H

#include "nodes.h"
#include "mariadb_func.h"
#include "mariadb_nodes.h"

class Maxscales : public Nodes
{
public:
    enum service
    {
        RWSPLIT,
        READCONN_MASTER,
        READCONN_SLAVE
    };

    Maxscales(const char* pref, const char* test_cwd, bool verbose);
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
     * @brief Get port number of a MaxScale service
     *
     * @param type Type of service
     * @param m    MaxScale instance to use
     *
     * @return Port number of the service
     */
    int port(enum service type = RWSPLIT, int m = 0) const;

    /**
     * @brief binlog_port binlog router service port
     */
    int binlog_port[256];

    /**
     * @brief conn_rwsplit  MYSQL connection struct to RWSplit service
     */
    MYSQL* conn_rwsplit[256];

    /**
     * @brief conn_master   MYSQL connection struct to ReadConnection in master mode service
     */
    MYSQL* conn_master[256];

    /**
     * @brief conn_slave MYSQL connection struct to ReadConnection in slave mode service
     */
    MYSQL* conn_slave[256];

    /**
     * @brief routers Array of 3 MYSQL handlers which contains copies of conn_rwsplit, conn_master, conn_slave
     */
    MYSQL* routers[256][3];

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
     * @brief ConnectMaxscale   Opens connections to RWSplit, ReadConn master and ReadConn slave Maxscale
     * services
     * Opens connections to RWSplit, ReadConn master and ReadConn slave Maxscale services
     * Connections stored in maxscales->conn_rwsplit[0], maxscales->conn_master[0] and
     * maxscales->conn_slave[0] MYSQL structs
     * @return 0 in case of success
     */
    int connect_maxscale(int m = 0, const std::string& db = "test");
    int connect(int m = 0, const std::string& db = "test")
    {
        return connect_maxscale(m, db);
    }

    /**
     * @brief CloseMaxscaleConn Closes connection that were opened by ConnectMaxscale()
     * @return 0
     */
    int close_maxscale_connections(int m = 0);
    int disconnect(int m = 0)
    {
        return close_maxscale_connections(m);
    }

    /**
     * @brief ConnectRWSplit    Opens connections to RWSplit and store MYSQL struct in
     * maxscales->conn_rwsplit[0]
     * @return 0 in case of success
     */
    int connect_rwsplit(int m = 0, const std::string& db = "test");

    /**
     * @brief ConnectReadMaster Opens connections to ReadConn master and store MYSQL struct in
     * maxscales->conn_master[0]
     * @return 0 in case of success
     */
    int connect_readconn_master(int m = 0, const std::string& db = "test");

    /**
     * @brief ConnectReadSlave Opens connections to ReadConn slave and store MYSQL struct in
     * maxscales->conn_slave[0]
     * @return 0 in case of success
     */
    int connect_readconn_slave(int m = 0, const std::string& db = "test");

    /**
     * @brief OpenRWSplitConn   Opens new connections to RWSplit and returns MYSQL struct
     * To close connection mysql_close() have to be called
     * @return MYSQL struct
     */
    MYSQL* open_rwsplit_connection(int m = 0, const std::string& db = "test")
    {
        return open_conn(rwsplit_port[m], IP[m], user_name, password, ssl);
    }

    /**
     * Get a readwritesplit Connection
     */
    Connection rwsplit(int m = 0, const std::string& db = "test")
    {
        return Connection(IP[m], rwsplit_port[m], user_name, password, db, ssl);
    }

    /**
     * @brief OpenReadMasterConn    Opens new connections to ReadConn master and returns MYSQL struct
     * To close connection mysql_close() have to be called
     * @return MYSQL struct
     */
    MYSQL* open_readconn_master_connection(int m = 0)
    {
        return open_conn(readconn_master_port[m],
                         IP[m],
                         user_name,
                         password,
                         ssl);
    }

    /**
     * Get a readconnroute master Connection
     */
    Connection readconn_master(int m = 0, const std::string& db = "test")
    {
        return Connection(IP[m], readconn_master_port[m], user_name, password, db, ssl);
    }

    /**
     * @brief OpenReadSlaveConn    Opens new connections to ReadConn slave and returns MYSQL struct
     * To close connection mysql_close() have to be called
     * @return  MYSQL struct
     */
    MYSQL* open_readconn_slave_connection(int m = 0)
    {
        return open_conn(readconn_slave_port[m],
                         IP[m],
                         user_name,
                         password,
                         ssl);
    }

    /**
     * Get a readconnroute slave Connection
     */
    Connection readconn_slave(int m = 0, const std::string& db = "test")
    {
        return Connection(IP[m], readconn_slave_port[m], user_name, password, db, ssl);
    }

    /**
     * @brief CloseRWSplit Closes RWplit connections stored in maxscales->conn_rwsplit[0]
     */
    void close_rwsplit(int m = 0)
    {
        mysql_close(conn_rwsplit[m]);
        conn_rwsplit[m] = NULL;
    }

    /**
     * @brief CloseReadMaster Closes ReadConn master connections stored in maxscales->conn_master[0]
     */
    void close_readconn_master(int m = 0)
    {
        mysql_close(conn_master[m]);
        conn_master[m] = NULL;
    }

    /**
     * @brief CloseReadSlave Closes ReadConn slave connections stored in maxscales->conn_slave[0]
     */
    void close_readconn_slave(int m = 0)
    {
        mysql_close(conn_slave[m]);
        conn_slave[m] = NULL;
    }

    /**
     * @brief restart_maxscale Issues 'service maxscale restart' command
     */
    int restart_maxscale(int m = 0);
    int restart(int m = 0)
    {
        return restart_maxscale(m);
    }

    /**
     * @brief alias for restart_maxscale
     */
    int start_maxscale(int m = 0)
    {
        return restart_maxscale(m);
    }
    int start(int m = 0)
    {
        return start_maxscale(m);
    }

    /**
     * @brief stop_maxscale Issues 'service maxscale stop' command
     */
    int stop_maxscale(int m = 0);
    int stop(int m = 0)
    {
        return stop_maxscale(m);
    }

    // Helper for stopping all maxscales
    void stop_all()
    {
        for (int i = 0; i < N; i++)
        {
            stop(i);
        }
    }

    int execute_maxadmin_command(int m, const char* cmd);
    int execute_maxadmin_command_print(int m, const char* cmd);
    int check_maxadmin_param(int m, const char* command, const char* param, const char* value);
    int get_maxadmin_param(int m, const char* command, const char* param, char* result);

    /**
     * @brief get_maxscale_memsize Gets size of the memory consumed by Maxscale process
     * @return memory size in kilobytes
     */
    long unsigned get_maxscale_memsize(int m = 0);

    /**
     * @brief find_master_maxadmin Tries to find node with 'Master' status using Maxadmin connand 'show
     * server'
     * @param nodes Mariadb_nodes object
     * @return node index if one master found, -1 if no master found or several masters found
     */
    int find_master_maxadmin(Mariadb_nodes* nodes, int m = 0);
    int find_slave_maxadmin(Mariadb_nodes* nodes, int m = 0);

    /**
     * @brief Get the set of labels that are assigned to server @c name
     *
     * @param name The name of the server that must be present in the output `maxadmin list servers`
     *
     * @param m Number of Maxscale node
     *
     * @return A set of string labels assigned to this server
     */
    StringSet get_server_status(const char* name, int m = 0);

    /**
     * Wait until the monitors have performed at least one monitoring operation
     *
     * The function waits until all monitors have performed at least one monitoring cycle.
     *
     * @param intervals The number of monitor intervals to wait
     * @param m Number of Maxscale node
     */
    void wait_for_monitor(int intervals = 1, int m = 0);
};

#endif      // MAXSCALES_H
