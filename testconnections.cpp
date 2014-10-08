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
    printf("Reading env\n");
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
    conn_rwsplit = open_conn(4006, Maxscale_IP);
    conn_master  = open_conn(4008, Maxscale_IP);
    conn_slave   = open_conn(4009, Maxscale_IP);
}


int TestConnections::CloseMaxscaleConn()
{
    mysql_close(conn_master);
    mysql_close(conn_slave);
    mysql_close(conn_rwsplit);
}

