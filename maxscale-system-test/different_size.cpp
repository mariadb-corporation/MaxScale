#include <iostream>
#include <unistd.h>
#include "testconnections.h"

using namespace std;

char * create_event_size(unsigned long size)
{
    char * prefix  = (char *) "insert into test.large_event values (1, '";
    unsigned long prefix_size = strlen(prefix);
    char * postfix = (char *) "');" ;
    char * event = (char*)malloc(size + 1);
    strcpy(event, prefix);

    unsigned long max = size - 55 - 45;


    //printf("BLOB data size %lu\n", max);

    for (unsigned long i =  0; i < max; i++)
    {
        event[i + prefix_size] = 'a';
    }

    strcpy((char *) event + max + prefix_size, postfix);
    return event;
}

MYSQL * connect_to_serv(TestConnections* Test, bool binlog)
{
    MYSQL * conn;
    if (binlog)
    {
        conn = open_conn(Test->repl->port[0], Test->repl->IP[0], Test->repl->user_name, Test->repl->password,
                         Test->ssl);
    }
    else
    {
        conn = Test->maxscales->open_rwsplit_connection(0);
    }
    return conn;
}

void set_max_packet(TestConnections* Test, bool binlog, char * cmd)
{
    Test->tprintf("Setting maximum packet size ...");
    if (binlog)
    {
        Test->repl->connect();
        Test->try_query(Test->repl->nodes[0], cmd);
        Test->repl->close_connections();
    }
    else
    {
        Test->maxscales->connect_maxscale(0);
        Test->try_query(Test->maxscales->conn_rwsplit[0], cmd);
        Test->maxscales->close_maxscale_connections(0);
    }
    Test->tprintf(".. done\n");
}

void different_packet_size(TestConnections* Test, bool binlog)
{
    Test->set_timeout(60);
    Test->tprintf("Set big max_allowed_packet\n");
    set_max_packet(Test, binlog,  (char *) "set global max_allowed_packet = 200000000;");

    Test->set_timeout(40);
    Test->tprintf("Create table\n");
    MYSQL* conn = connect_to_serv(Test, binlog);
    Test->try_query(conn, "DROP TABLE IF EXISTS test.large_event;"
                    "CREATE TABLE test.large_event(id INT, data LONGBLOB);");
    mysql_close(conn);

    const int loops = 3;
    const int range = 2;

    for (int i = 1; i <= loops; i++)
    {
        for (int j = -range; j <= range; j++)
        {
            size_t size = 0x0ffffff * i + j;
            Test->tprintf("Trying event app. %lu bytes", size);
            Test->set_timeout(240);

            char* event = create_event_size(size);
            conn = connect_to_serv(Test, binlog);
            Test->expect(execute_query_silent(conn, event) == 0, "Query should succeed");
            free(event);
            execute_query_silent(conn, (char *) "DELETE FROM test.large_event");
            mysql_close(conn);
        }
    }

    Test->set_timeout(40);
    Test->tprintf("Restoring max_allowed_packet");
    set_max_packet(Test, binlog,  (char *) "set global max_allowed_packet = 1048576;");

    Test->set_timeout(300);
    conn = connect_to_serv(Test, binlog);
    Test->try_query(conn, "DROP TABLE test.large_event");
    mysql_close(conn);

}
