#include "testconnections.h"


TestConnections::TestConnections()
{
    galera = new Mariadb_nodes((char *)"galera");
    repl   = new Mariadb_nodes((char *)"repl");
}

int TestConnections::ReadEnv()
{

    char * env;
    int i;
    printf("Reading test setup configuration from environmental variables\n");
    galera->ReadEnv();
    repl->ReadEnv();

    env = getenv("Maxscale_IP"); if (env != NULL) {sprintf(Maxscale_IP, "%s", env);}
}

int TestConnections::PrintIP()
{
    int  i;
    printf("Maxscale IP\t%s\n", Maxscale_IP);
    repl->PrintIP();
    galera->PrintIP();
}

int TestConnections::ConnectMaxscale()
{
    conn_rwsplit = ConnectRWSplit();
    conn_master  = ConnectReadMaster();
    conn_slave   = ConnectReadSlave();
}

int TestConnections::CloseMaxscaleConn()
{
    mysql_close(conn_master);
    mysql_close(conn_slave);
    mysql_close(conn_rwsplit);
}

