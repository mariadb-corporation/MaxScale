#ifndef TESTCONNECTIONS_H
#define TESTCONNECTIONS_H

#include "mariadb_nodes.h"

const int rwsplit_port = 4006;
const int readconn_master_port = 4008;
const int readconn_slave_port = 4009;

class TestConnections
{
public:
    TestConnections();

    MYSQL *conn_rwsplit;
    MYSQL *conn_master;
    MYSQL *conn_slave;

    Mariadb_nodes * galera;
    Mariadb_nodes * repl;

    char Maxscale_IP[16];

    int ReadEnv();
    int PrintIP();
    int ConnectMaxscale();
    int CloseMaxscaleConn();

    MYSQL * ConnectRWSplit() {return open_conn(rwsplit_port, Maxscale_IP);}
    MYSQL * ConnectReadMaster() {return open_conn(readconn_master_port, Maxscale_IP);}
    MYSQL * ConnectReadSlave() {return open_conn(readconn_slave_port, Maxscale_IP);}


};

#endif // TESTCONNECTIONS_H
