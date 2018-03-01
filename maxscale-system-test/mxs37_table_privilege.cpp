/**
 * @file mxs37_table_privilege.cpp mxs37 (bug719) regression case ("mandatory SELECT privilege on db level?")
 * - create user with only 'SELECT' priveledge
 * - try to connecto to MAxscle with this user
 */


#include <iostream>
#include <unistd.h>
#include "testconnections.h"
#include "sql_t1.h"

using namespace std;

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);
    Test->set_timeout(60);

    Test->connect_maxscale();

    Test->tprintf("Create user with only SELECT priviledge to a table");

    execute_query_silent(Test->conn_rwsplit, "DROP USER 'table_privilege'@'%'");
    execute_query_silent(Test->conn_rwsplit, "DROP TABLE test.t1");
    execute_query(Test->conn_rwsplit, "CREATE TABLE test.t1 (id INT)");
    execute_query(Test->conn_rwsplit, "CREATE USER 'table_privilege'@'%%' IDENTIFIED BY 'pass'");
    execute_query(Test->conn_rwsplit, "GRANT SELECT ON test.t1 TO 'table_privilege'@'%%'");

    Test->stop_timeout();
    Test->repl->sync_slaves();

    Test->tprintf("Trying to connect using this user\n");
    Test->set_timeout(20);

    bool error = true;

    /**
     * Since this test is executed on both Galera and Master-Slave clusters, we
     * need to try to connect multiple times as Galera user creation doesn't
     * seem to apply instantly on all nodes. For Master-Slave clusters, the
     * first connection should be OK and if it's not, it's highly likely that
     * others will also fail.
     */
    for (int i = 0; i < 5; i++)
    {
        MYSQL *conn = open_conn_db(Test->rwsplit_port, Test->maxscale_IP, (char *) "test",
                                   (char *) "table_privilege", (char *) "pass", Test->ssl);
        if (mysql_errno(conn) != 0)
        {
            Test->tprintf("Failed to connect: %s", mysql_error(conn));
        }
        else
        {
            Test->set_timeout(20);
            Test->tprintf("Trying SELECT\n");
            if (execute_query(conn, (char *) "SELECT * FROM t1") == 0)
            {
                error = false;
                break;
            }
        }
        mysql_close(conn);
        sleep(1);
    }

    if (error)
    {
        Test->add_result(1, "Failed to connect.");
    }

    Test->set_timeout(20);
    execute_query_silent(Test->conn_rwsplit, "DROP USER 'table_privilege'@'%'");
    execute_query_silent(Test->conn_rwsplit, "DROP TABLE test.t1");

    Test->check_maxscale_alive();
    int rval = Test->global_result;
    delete Test;

    return rval;
}

