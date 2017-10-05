/**
 * @file bug565.cpp  regression case for bug 565 ( "Clients CLIENT_FOUND_ROWS setting is ignored by maxscale" ) MAX-311
 *
 * - open connection with CLIENT_FOUND_ROWS flag
 * - CREATE TABLE t1(id INT PRIMARY KEY, val INT, msg VARCHAR(100))
 * - INSERT INTO t1 VALUES (1, 1, 'foo'), (2, 1, 'bar'), (3, 2, 'baz'), (4, 2, 'abc')"
 * - check 'affected_rows' for folloing UPDATES:
 *   + UPDATE t1 SET msg='xyz' WHERE val=2" (expect 2)
 *   + UPDATE t1 SET msg='xyz' WHERE val=2 (expect 0)
 *   + UPDATE t1 SET msg='xyz' WHERE val=2 (expect 2)
 */

/*
Hartmut Holzgraefe 2014-10-02 14:27:18 UTC
Created attachment 155 [details]
test for mysql_affected_rows() with/without CLIENT_FOUND_ROWS connection flag

Even worse: connections via maxscale always behave as if CLIENT_FOUND_ROWS is set even though the default is NOT having it set.

When doing the same update two times in a row without CLIENT_FOUND_ROWS
mysql_affected_rows() should return the number of rows actually changed
by the last query, while with CLIENT_FOUND_ROWS connection flag set the
number of matching rows is returned, even if the UPDATE didn't change
any column values.

With a direct connection to mysqld this works as expected,
through readconnroute(master) I'm always getting the number of matching
rows (as if CLIENT_FOUND_ROWS was set), and not the number of actually
changed rows when CLIENT_FOUND_ROWS is not set (which is the default
behaviour when not setting connection options)

Attaching PHP mysqli test script, result with direct mysqld connection is

  update #1: 2
  update #2: 0
  update #3: 2

while through maxscale it is

  update #1: 2
  update #2: 2
  update #3: 2

I also verified this using the C API directly to rule out that this is
a PHP specific problem
Comment 1 Vilho Raatikka 2014-10-08 14:11:38 UTC
Client flags are not passed to backend server properly.
Comment 2 Vilho Raatikka 2014-10-08 19:35:58 UTC
Pushed initial fix to MAX-311. Waiting for validation for the fix.
*/


#include <iostream>
#include "testconnections.h"

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);
    Test->set_timeout(30);
    MYSQL * conn_found_rows;
    my_ulonglong rows;


    Test->repl->connect();
    Test->connect_maxscale();

    conn_found_rows = open_conn_db_flags(Test->maxscales->rwsplit_port[0], Test->maxscales->IP[0], (char *) "test",
                                         Test->maxscales->user_name, Test->maxscales->password, CLIENT_FOUND_ROWS, Test->ssl);

    Test->set_timeout(30);
    execute_query(Test->maxscales->conn_rwsplit[0], "DROP TABLE IF EXISTS t1");
    execute_query(Test->maxscales->conn_rwsplit[0], "CREATE TABLE t1(id INT PRIMARY KEY, val INT, msg VARCHAR(100))");
    execute_query(Test->maxscales->conn_rwsplit[0],
                  "INSERT INTO t1 VALUES (1, 1, 'foo'), (2, 1, 'bar'), (3, 2, 'baz'), (4, 2, 'abc')");

    Test->set_timeout(30);
    execute_query_affected_rows(Test->maxscales->conn_rwsplit[0], "UPDATE t1 SET msg='xyz' WHERE val=2", &rows);
    Test->tprintf("update #1: %ld (expeced value is 2)\n", (long) rows);
    if (rows != 2)
    {
        Test->add_result(1, "Affected rows is not 2\n");
    }

    Test->set_timeout(30);
    execute_query_affected_rows(Test->maxscales->conn_rwsplit[0], "UPDATE t1 SET msg='xyz' WHERE val=2", &rows);
    Test->tprintf("update #2: %ld  (expeced value is 0)\n", (long) rows);
    if (rows != 0)
    {
        Test->add_result(1, "Affected rows is not 0\n");
    }

    Test->set_timeout(30);
    execute_query_affected_rows(conn_found_rows, "UPDATE t1 SET msg='xyz' WHERE val=2", &rows);
    Test->tprintf("update #3: %ld  (expeced value is 2)\n", (long) rows);
    if (rows != 2)
    {
        Test->add_result(1, "Affected rows is not 2\n");
    }

    Test->close_maxscale_connections();

    int rval = Test->global_result;
    delete Test;
    return rval;

}
