/**
 * @file transaction_test_wo_maxscale.cpp
 */


#include <iostream>
#include "testconnections.h"
#include "maxadmin_operations.h"
#include "sql_t1.h"

int check_sha1(TestConnections* Test)
{
    Test->tprintf("ls before FLUSH LOGS\n");

    Test->tprintf("Master");
    Test->repl->ssh_node(0, (char *) "ls -la /var/lib/mysql/mar-bin.0000*", false);

    Test->tprintf("FLUSH LOGS\n");
    Test->try_query(Test->repl->nodes[0], (char *) "FLUSH LOGS");
    Test->tprintf("Logs flushed\n");
    sleep(20);
    Test->tprintf("ls after first FLUSH LOGS\n");

    Test->tprintf("Master\n");
    Test->repl->ssh_node(0, (char *) "ls -la /var/lib/mysql/mar-bin.0000*", false);

    Test->tprintf("FLUSH LOGS\n");
    Test->try_query(Test->repl->nodes[0], (char *) "FLUSH LOGS");
    Test->tprintf("Logs flushed\n");
    fflush(stdout);

    sleep(19);
    printf("ls before FLUSH LOGS\n");

    printf("Master");
    Test->repl->ssh_node(0, (char *) "ls -la /var/lib/mysql/mar-bin.0000*", false);

    return Test->global_result;
}

int start_transaction(TestConnections* Test)
{
    int global_result = 0;
    Test->tprintf("Transaction test\n");
    Test->tprintf("Start transaction\n");
    global_result += execute_query(Test->repl->nodes[0], (char *) "START TRANSACTION");
    //global_result += execute_query(Test->repl->nodes[0], (char *) "SET autocommit = 0");
    Test->tprintf("INSERT data\n");
    global_result += execute_query(Test->repl->nodes[0], (char *) "INSERT INTO t1 VALUES(111, 10)");
    sleep(20);
    return global_result;
}

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);

    int i;

    for (int option = 0; option < 3; option++)
    {

        Test->repl->connect();

        create_t1(Test->repl->nodes[0]);
        Test->add_result( insert_into_t1(Test->repl->nodes[0], 4), "INSER into t1 failed\n");
        Test->tprintf("Sleeping to let replication happen\n");
        sleep(30);

        for (i = 0; i < Test->repl->N; i++)
        {
            Test->tprintf("Checking data from node %d (%s)\n", i, Test->repl->IP[i]);
            Test->add_result( select_from_t1(Test->repl->nodes[i], 4), "select form t1 wrong\n");
        }

        Test->tprintf("First transaction test (with ROLLBACK)\n");
        start_transaction(Test);

        Test->tprintf("SELECT * FROM t1 WHERE fl=10, checking inserted values\n");
        Test->add_result( execute_query_check_one(Test->repl->nodes[0], (char *) "SELECT * FROM t1 WHERE fl=10",
                          "111"), "failed\n");

        //printf("SELECT, checking inserted values from slave\n");
        //global_result += execute_query_check_one(Test->repl->nodes[2], (char *) "SELECT * FROM t1 WHERE fl=10", "111");

        Test->add_result( check_sha1(Test), "sha1 wrong\n");

        Test->tprintf("ROLLBACK\n");
        Test->try_query(Test->repl->nodes[0], (char *) "ROLLBACK");
        Test->tprintf("INSERT INTO t1 VALUES(112, 10)\n");
        Test->try_query(Test->repl->nodes[0], (char *) "INSERT INTO t1 VALUES(112, 10)");
        sleep(20);

        Test->tprintf("SELECT * FROM t1 WHERE fl=10, checking inserted values\n");
        Test->add_result( execute_query_check_one(Test->repl->nodes[0], (char *) "SELECT * FROM t1 WHERE fl=10",
                          "112"), "failed\n");

        Test->tprintf("SELECT * FROM t1 WHERE fl=10, checking inserted values from slave\n");
        Test->add_result( execute_query_check_one(Test->repl->nodes[2], (char *) "SELECT * FROM t1 WHERE fl=10",
                          "112"), "failed\n");
        Test->tprintf("DELETE FROM t1 WHERE fl=10\n");
        Test->try_query(Test->repl->nodes[0], (char *) "DELETE FROM t1 WHERE fl=10");
        Test->tprintf("Checking t1\n");
        Test->add_result( select_from_t1(Test->repl->nodes[0], 4), "failed\n");

        Test->tprintf("Second transaction test (with COMMIT)\n");
        start_transaction(Test);

        Test->tprintf("COMMIT\n");
        Test->try_query(Test->repl->nodes[0], (char *) "COMMIT");

        printf("SELECT, checking inserted values\n");
        Test->add_result( execute_query_check_one(Test->repl->nodes[0], (char *) "SELECT * FROM t1 WHERE fl=10",
                          "111"), "failed\n");

        Test->tprintf("SELECT, checking inserted values from slave\n");
        Test->add_result( execute_query_check_one(Test->repl->nodes[2], (char *) "SELECT * FROM t1 WHERE fl=10",
                          "111"), "failed\n");
        Test->tprintf("DELETE FROM t1 WHERE fl=10\n");
        Test->try_query(Test->repl->nodes[0], (char *) "DELETE FROM t1 WHERE fl=10");

        Test->add_result( check_sha1(Test), "sha1 wrong\n");
        Test->repl->close_connections();
    }

    int rval = Test->global_result;
    delete Test;
    return rval;
}
