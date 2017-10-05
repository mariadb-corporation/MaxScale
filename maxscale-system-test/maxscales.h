#ifndef MAXSCALES_H
#define MAXSCALES_H

#include "nodes.h"
#include "mariadb_func.h"

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

};

#endif // MAXSCALES_H
