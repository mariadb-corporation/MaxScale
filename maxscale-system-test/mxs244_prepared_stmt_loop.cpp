/**
 * @file mxs244_prepared_stmt_loop.cpp mxs244_prepared_stmt_loop executed following statements in the loop against all routers:
 * @verbatim
SET NAMES "UTF8";
PREPARE s1 FROM 'SHOW GLOBAL STATUS WHERE variable_name = ?';
SET @a = "Com_stmt_prepare";
EXECUTE s1 USING @a;
PREPARE s1 FROM 'SHOW GLOBAL STATUS WHERE variable_name = ?';
SET @a = "Com_stmt_close";
EXECUTE s1 USING @a;
@endverbatim
 */

#include <iostream>
#include "testconnections.h"

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);
    long unsigned iterations = (Test->smoke) ? 1000 : 25000;
    int r = (Test->smoke) ? 1 : 3;

    Test->set_timeout(5);
    Test->repl->connect();
    Test->connect_maxscale();
    MYSQL * router[3];
    router[0] = Test->conn_rwsplit;
    router[1] = Test->conn_master;
    router[2] = Test->conn_slave;

    for (int ir = 0; ir < r; ir++)
    {
        Test->tprintf("Trying simple prepared statements in the loop, router %d\n", ir);
        for (long unsigned  i = 0; i < iterations; i++)
        {
            Test->set_timeout(10);
            Test->try_query(router[ir], (char *) "SET NAMES \"UTF8\"");
            Test->try_query(router[ir], (char *) "PREPARE s1 FROM 'SHOW GLOBAL STATUS WHERE variable_name = ?'");
            Test->try_query(router[ir], (char *) "SET @a = \"Com_stmt_prepare\"");
            Test->try_query(router[ir], (char *) "EXECUTE s1 USING @a");
            Test->try_query(router[ir], (char *) "PREPARE s1 FROM 'SHOW GLOBAL STATUS WHERE variable_name = ?'");
            Test->try_query(router[ir], (char *) "SET @a = \"Com_stmt_close\"");
            Test->try_query(router[ir], (char *) "EXECUTE s1 USING @a");
            if ((( i / 100) * 100) == i )
            {
                Test->tprintf("Iterations = %lu\n", i);
            }
        }
    }

    Test->set_timeout(20);

    Test->close_maxscale_connections();
    Test->check_maxscale_alive();

    int rval = Test->global_result;
    delete Test;
    return rval;

}
