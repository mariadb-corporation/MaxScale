/**
 * @file bug572.cpp  regression case for bug 572 ( " If reading a user from users table fails, MaxScale fails" )
 *
 * - try GRANT with wrong IP using all Maxscale services:
 *  + GRANT ALL PRIVILEGES ON *.* TO  'foo'@'*.foo.notexists' IDENTIFIED BY 'foo';
 *  + GRANT ALL PRIVILEGES ON *.* TO  'bar'@'127.0.0.*' IDENTIFIED BY 'bar'
 *  + DROP USER 'foo'@'*.foo.notexists'
 *  + DROP USER 'bar'@'127.0.0.*'
 * - do "select * from mysql.user" using RWSplit to check if Maxsclae crashed
 */


#include <iostream>
#include <unistd.h>
#include "testconnections.h"

using namespace std;

void create_drop_bad_user(MYSQL * conn, TestConnections * Test)
{

    Test->try_query(conn, (char *)
                    "GRANT ALL PRIVILEGES ON *.* TO  'foo'@'*.foo.notexists' IDENTIFIED BY 'foo';");
    Test->try_query(conn, (char *) "GRANT ALL PRIVILEGES ON *.* TO  'bar'@'127.0.0.*' IDENTIFIED BY 'bar'");
    Test->try_query(conn, (char *) "DROP USER 'foo'@'*.foo.notexists'");
    Test->try_query(conn, (char *) "DROP USER 'bar'@'127.0.0.*'");
}

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);
    Test->set_timeout(10);

    Test->repl->connect();
    Test->maxscales->connect_maxscale(0);

    Test->tprintf("Trying GRANT for with bad IP: RWSplit\n");
    create_drop_bad_user(Test->maxscales->conn_rwsplit[0], Test);

    Test->tprintf("Trying SELECT to check if Maxscale hangs\n");
    Test->try_query(Test->maxscales->conn_rwsplit[0], (char *) "select * from mysql.user");

    int rval = Test->global_result;
    delete Test;
    return rval;
}
