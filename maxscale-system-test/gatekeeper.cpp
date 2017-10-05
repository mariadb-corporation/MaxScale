
#include <iostream>
#include <unistd.h>
#include "testconnections.h"

/**
 * Test for the gatekeeper module
 */

const char* training_queries[] =
{
    "SELECT * FROM test.t1 WHERE id = 1",
    "INSERT INTO test.t1 VALUES (1)",
    "UPDATE test.t1 SET id = 2 WHERE id = 1",
    NULL
};

const char* allowed_queries[] =
{
    "SELECT * FROM test.t1 WHERE id = 1",
    "SELECT * FROM test.t1 WHERE id = 2",
    "SELECT * FROM test.t1 WHERE id = 102",
    "INSERT INTO test.t1 VALUES (1)",
    "INSERT INTO test.t1 VALUES (124)",
    "INSERT INTO test.t1 VALUES (127419823)",
    "UPDATE test.t1 SET id = 4 WHERE id = 1",
    "UPDATE test.t1 SET id = 3 WHERE id = 2",
    "UPDATE test.t1 SET id = 2 WHERE id = 3",
    "UPDATE test.t1 SET id = 1 WHERE id = 4",
    "   UPDATE    test.t1    SET   id   =   1   WHERE   id   =   4    ",
    NULL
};

const char* denied_queries[] =
{
    "SELECT * FROM test.t1 WHERE id = 1 OR 1=1",
    "INSERT INTO test.t1 VALUES (1), ('This is not a number')",
    "UPDATE test.t1 SET id = 2 WHERE id = 1 OR id > 0",
    NULL
};

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);
    Test->ssh_maxscale(true, "rm -f /var/lib/maxscale/gatekeeper.data");
    Test->set_timeout(30);

    Test->connect_rwsplit();

    Test->try_query(Test->maxscales->conn_rwsplit[0], "CREATE OR REPLACE TABLE test.t1 (id INT)");

    for (int i = 0; training_queries[i]; i++)
    {
        Test->try_query(Test->maxscales->conn_rwsplit[0], training_queries[i]);
    }

    Test->close_rwsplit();

    Test->ssh_maxscale(true, "sed -i -e 's/mode=learn/mode=enforce/' /etc/maxscale.cnf");

    Test->restart_maxscale();

    sleep(5);

    Test->connect_rwsplit();

    for (int i = 0; training_queries[i]; i++)
    {
        Test->set_timeout(30);
        Test->add_result(execute_query(Test->maxscales->conn_rwsplit[0], training_queries[i]), "Query should not fail: %s",
                         training_queries[i]);
    }

    for (int i = 0; allowed_queries[i]; i++)
    {
        Test->set_timeout(30);
        Test->add_result(execute_query(Test->maxscales->conn_rwsplit[0], allowed_queries[i]), "Query should not fail: %s",
                         allowed_queries[i]);
    }

    for (int i = 0; denied_queries[i]; i++)
    {
        Test->set_timeout(30);
        Test->add_result(execute_query(Test->maxscales->conn_rwsplit[0], denied_queries[i]) == 0, "Query should fail: %s",
                         denied_queries[i]);
    }

    Test->ssh_maxscale(true, "rm -f /var/lib/maxscale/gatekeeper.data");
    int rval = Test->global_result;
    delete Test;
    return rval;
}
