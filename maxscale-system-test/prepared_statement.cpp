/**
 * @file prepared_statement.cpp Checks if prepared statement works via Maxscale
 *
 * - Create table t1 and fill it ith some data
 * - via RWSplit:
 *   + PREPARE stmt FROM 'SELECT * FROM t1 WHERE fl=@x;';
 *   + SET @x = 3;")
 *   + EXECUTE stmt")
 *   + SET @x = 4;")
 *   + EXECUTE stmt")
 * - check if Maxscale is alive
 */


#include <iostream>
#include "testconnections.h"
#include "sql_t1.h"

using namespace std;

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);
    Test->set_timeout(60);
    int N = 4;

    Test->repl->connect();
    if (Test->connect_maxscale() != 0 )
    {
        printf("Error connecting to MaxScale\n");
        delete Test;
        exit(1);
    }

    create_t1(Test->conn_rwsplit);
    insert_into_t1(Test->conn_rwsplit, N);

    Test->set_timeout(20);
    Test->try_query(Test->conn_rwsplit, (char *) "PREPARE stmt FROM 'SELECT * FROM t1 WHERE fl=@x;';");
    Test->try_query(Test->conn_rwsplit, (char *) "SET @x = 3;");
    Test->try_query(Test->conn_rwsplit, (char *) "EXECUTE stmt");
    Test->try_query(Test->conn_rwsplit, (char *) "SET @x = 4;");
    Test->try_query(Test->conn_rwsplit, (char *) "EXECUTE stmt");

    Test->check_maxscale_alive();
    int rval = Test->global_result;
    delete Test;
    return rval;
}


/*
  Hi Timofey, I can't imagine repeatable way to run in to the situation where session command replies would arrive in different order, at least without additional instrumentation. You can, however, increase the probability for it to occur by setting up master and multiple slaves, the more the better.

Then start to prepare statements which return much data. The length of response packet depends on number of columns so good query would be something that produces lots of columns, like select a.user, b.user, c.user, ... z.user from mysql.user a, mysql.user b, mysql.user c, ... mysql.user z

I'm not sure that it will happen but it is possible. You can also try to make it happen by decreasing the size of network packet because the smaller that is the more likely it is that responses are split into multiple packets - which can then becomen interleaved with packets from different slaves.
*/
