#ifndef TESTCONNECTIONS_H
#define TESTCONNECTIONS_H

#include "mariadb_nodes.h"

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

};

#endif // TESTCONNECTIONS_H
