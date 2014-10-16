#ifndef TESTCONNECTIONS_H
#define TESTCONNECTIONS_H

#include "mariadb_nodes.h"


class TestConnections
{
public:
    TestConnections();

    int rwsplit_port;
    int readconn_master_port;
    int readconn_slave_port;

    MYSQL *conn_rwsplit;
    MYSQL *conn_master;
    MYSQL *conn_slave;

    Mariadb_nodes * galera;
    Mariadb_nodes * repl;

    char Maxscale_IP[16];
    char KillVMCommand[4096];
    char GetLogsCommand[4096];

    int ReadEnv();
    int PrintIP();
    int ConnectMaxscale();
    int CloseMaxscaleConn();

    int ConnectRWSplit() {conn_rwsplit = open_conn(rwsplit_port, Maxscale_IP); if (conn_rwsplit == NULL){return(1);} else {return(0);}}
    int ConnectReadMaster() {conn_master = open_conn(readconn_master_port, Maxscale_IP);  if (conn_rwsplit == NULL){return(1);} else {return(0);}}
    int ConnectReadSlave() {conn_slave = open_conn(readconn_slave_port, Maxscale_IP); if (conn_rwsplit == NULL){return(1);} else {return(0);}}

    MYSQL * OpenRWSplitConn() {return open_conn(rwsplit_port, Maxscale_IP);}
    MYSQL * OpenReadMasterConn() {return open_conn(readconn_master_port, Maxscale_IP);}
    MYSQL * OpenReadSlaveConn() {return open_conn(readconn_slave_port, Maxscale_IP);}

    void CloseRWSplit(){mysql_close(conn_rwsplit);}
    void CloseReadMaster(){mysql_close(conn_master);}
    void CloseREADSlave(){mysql_close(conn_slave);}


};

int CheckLogErr(char * err_msg, bool expected);
int FindConnectedSlave(TestConnections* Test, int * global_result);

#endif // TESTCONNECTIONS_H
