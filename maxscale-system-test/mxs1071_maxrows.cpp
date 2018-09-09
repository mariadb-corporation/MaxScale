/**
 * @file mxs1071_maxrows.cpp Test of Maxrows filter
 * Initial filter configuration
 *  @verbatim
 *  [MaxRows]
 *  type=filter
 *  module=maxrows
 *  max_resultset_rows=20
 *  max_resultset_size=9000000
 *  debug=3
 *  @endverbatim
 * All the tests executes statemet, prepared statement or stored procedure and checks
 * number of rows in the result sets (multiple result sets possible)
 *
 * Test  1 - max_allowed_packet limit is not hit, simple SELECTs, small table
 * Test  2 - same queries, but bigger table - limit is hit in some cases
 * Test  3 - stored procedure, limit is not hit, single result set
 * Test  4 - stored procedure, limit is not hit, multiple result sets
 * Test  5 - stored procedure, limit is not hit, multiple result sets
 * Test  6 - stored procedure, limit is hit, multiple result sets
 * Test  7 - stored procedure, limit is not hit, long blobs, multiple result sets
 * Test  8 - stored procedure, limit is hit, long blobs, multiple result sets
 * Test  9 - query non-existant table, expect proper error
 * Test 10 - stored procedure, limit could be hit if executed until the end,
 *           multiple result sets, query non-existant table, expect proper error
 *           and result sets generated before error
 * Test 11 - SET @a=4 - empty result set
 * Test 12 - prepared statement, using mysql_stmt_* functions, limit is hit
 *           Test 12 is repeated using mysql_query() function
 * Test 13 - same as Test 12, but limit is not hit
 * Test 14 - prepared statement inside of store procedure, multiple result sets
 *           limit is not hit
 * Test 15 - prepared statement inside of store procedure, multiple result sets
 *           limit is hit
 * Test 16 - SELECT '' as 'A' limit 1 (empty result)
 * Test 17 - multiple result sets with empty result, limit is not hit
 * Test 18 - multiple result sets with empty result, exactly as a limit (20)
 *           (expect 20 result sets)
 * Test 19 - multiple result sets with empty result, limit is hit
 * Test 20 - SELECT long blobs, limit is not hit
 * Test 21 - change max_resultset_size to lower values, SELECT long blobs,
 *           max_resultset_size limit is hit
 */

#include <iostream>
#include "testconnections.h"
#include "sql_t1.h"
#include "mariadb_func.h"
#include "blob_test.h"

using namespace std;

const char* test03_sql
    = " CREATE PROCEDURE multi()\n"
      "BEGIN\n"
      "SELECT x1 FROM t1 LIMIT 2;\n"
      "END";

const char* test04_sql
    = "CREATE PROCEDURE multi() BEGIN\n"
      "SELECT 1;\n"
      "SELECT x1 FROM t1 LIMIT 2;\n"
      "SELECT 1,2,3; \n"
      "END";

const char* test05_sql
    = "CREATE PROCEDURE multi() BEGIN\n"
      "SELECT 1;\n"
      "SELECT x1 FROM t1 LIMIT 8;\n"
      "SELECT 1,2,3; \n"
      "SELECT 1;"
      "END";

const char* test06_sql
    = "CREATE PROCEDURE multi() BEGIN\n"
      "SELECT 1;\n"
      "SELECT x1 FROM t1 LIMIT 18;\n"
      "SELECT 2; \n"
      "SELECT 2;"
      "END";

const char* test07_sql
    = "CREATE PROCEDURE multi() BEGIN\n"
      "SELECT 1,2,3,4;\n"
      "SELECT id, b from long_blob_table order by id desc limit 1;\n"
      "SELECT id, b from long_blob_table order by id desc limit 4;\n"
      "SELECT id, b from long_blob_table order by id desc limit 1;\n"
      "SELECT id, b from long_blob_table order by id desc;\n"
      "SELECT id, b from long_blob_table order by id desc;\n"
      "SELECT 1;\n"
      "END";

const char* test08_sql
    = "CREATE PROCEDURE multi() BEGIN\n"
      "SELECT 1,2,3;\n"
      "SELECT id, b, b from long_blob_table order by id desc limit 1;\n"
      "SELECT 2;\n"
      "SELECT id, b from long_blob_table order by id desc limit 4;\n"
      "SELECT id, b from long_blob_table order by id desc limit 2;\n"
      "SELECT 1;\n"
      "SELECT 1;\n"
      "SELECT x1 FROM t1 LIMIT 8;\n"
      "SELECT 1;\n"
      "SELECT 1,2,3,4;\n"
      "END";

const char* test10_sql
    = "CREATE PROCEDURE multi() BEGIN\n"
      "SELECT 1;\n"
      "SELECT x1 FROM t1 limit 4;\n"
      "select * from dual;\n"
      "set @a=4;\n"
      "SELECT 2;\n"
      "SELECT * FROM t1;\n"
      "END";

const char* test14_sql
    = "CREATE PROCEDURE multi() BEGIN\n"
      "SELECT 1,3;\n"
      "SET @table = 't1';\n"
      "SET @s = CONCAT('SELECT * FROM ', @table, ' LIMIT 18');\n"
      "PREPARE stmt1 FROM @s;\n"
      "EXECUTE stmt1;\n"
      "DEALLOCATE PREPARE stmt1;\n"
      "SELECT 2,4,5;\n"
      "END";

const char* test15_sql
    = "CREATE PROCEDURE multi() BEGIN\n"
      "SELECT 1,3;\n"
      "SET @table = 't1';\n"
      "SET @s = CONCAT('SELECT * FROM ', @table, ' LIMIT 100');\n"
      "PREPARE stmt1 FROM @s;\n"
      "EXECUTE stmt1;\n"
      "DEALLOCATE PREPARE stmt1;\n"
      "SELECT 2,4,5;\n"
      "END";

const char* test17_sql
    = "CREATE PROCEDURE multi() BEGIN\n"
      "SELECT '' as 'A' limit 1;\n"
      "SELECT '' as 'A' limit 10;\n"
      "SELECT '' as 'A';\n"
      "END";

const char* test18_sql
    = "CREATE PROCEDURE multi() BEGIN\n"
      "SELECT '' as 'A' limit 1;\n"
      "SELECT '' as 'A' limit 10;\n"
      "SELECT '' as 'A';\n"
      "SELECT '' as 'A';\n"
      "SELECT '' as 'A';\n"
      "SELECT '' as 'A';\n"
      "SELECT '' as 'A';\n"
      "SELECT '' as 'A';\n"
      "SELECT '' as 'A';\n"
      "SELECT '' as 'A';\n"
      "SELECT '' as 'A' limit 1;\n"
      "SELECT '' as 'A' limit 10;\n"
      "SELECT '' as 'A';\n"
      "SELECT '' as 'A';\n"
      "SELECT '' as 'A';\n"
      "SELECT '' as 'A';\n"
      "SELECT '' as 'A';\n"
      "SELECT '' as 'A';\n"
      "SELECT '' as 'A';\n"
      "SELECT '' as 'A';\n"
      "END";

const char* test19_sql
    = "CREATE PROCEDURE multi() BEGIN\n"
      "SELECT '' as 'A' limit 1;\n"
      "SELECT '' as 'A' limit 10;\n"
      "SELECT '' as 'A';\n"
      "SELECT '' as 'A';\n"
      "SELECT '' as 'A';\n"
      "SELECT '' as 'A';\n"
      "SELECT '' as 'A';\n"
      "SELECT '' as 'A';\n"
      "SELECT '' as 'A';\n"
      "SELECT '' as 'A';\n"
      "SELECT '' as 'A' limit 1;\n"
      "SELECT '' as 'A' limit 10;\n"
      "SELECT '' as 'A';\n"
      "SELECT '' as 'A';\n"
      "SELECT '' as 'A';\n"
      "SELECT '' as 'A';\n"
      "SELECT '' as 'A';\n"
      "SELECT '' as 'A';\n"
      "SELECT '' as 'A';\n"
      "SELECT '' as 'A';\n"
      "SELECT '' as 'A';\n"
      "END";

/**
 * @brief compare_expected Execute sql and compare number of rows in every result set with expected values
 * If number if result sets differs from expected value or number of rows in any result sey differs from
 * given expected value this function calls Test->add_result
 * @param Test TestConnections object
 * @param sql SQL query to execute
 * @param exp_i Expected number of result sets
 * @param exp_rows Array of expected numbers of rows for every result set
 * @return 0 in case of lack of error
 */
int compare_expected(TestConnections* Test, const char* sql, my_ulonglong exp_i, my_ulonglong exp_rows[])
{
    my_ulonglong* rows = new my_ulonglong[30];
    my_ulonglong i;

    Test->set_timeout(30);
    execute_query_num_of_rows(Test->maxscales->conn_rwsplit[0], sql, rows, &i);

    Test->tprintf("Result sets number is %llu\n", i);

    if (i != exp_i)
    {
        Test->add_result(1, "Number of result sets is %llu instead of %llu\n", i, exp_i);
        return 1;
    }

    for (my_ulonglong j = 0; j < i; j++)
    {
        Test->tprintf("For result set %llu number of rows is %llu\n", j, rows[j]);
        if (rows[j] != exp_rows[j])
        {
            Test->add_result(1,
                             "For result set %llu number of rows is %llu instead of %llu\n",
                             j,
                             rows[j],
                             exp_rows[j]);
            return 1;
        }
    }
    return 0;
}

/**
 * @brief compare_stmt_expected Execute prepared statement and compare number of rows in every result set with
 *expected values
 * This function uses mysql_stmt-* functions (COM_STMT_EXECUTE, COM_STMT_FETCH)
 * @param Test TestConnections object
 * @param stmt MYSQL_STMT prepared statement handler
 * @param exp_i Expected number of result sets
 * @param exp_rows Array of expected numbers of rows for every result set
 * @return 0 in case of lack of error
 */
int compare_stmt_expected(TestConnections* Test,
                          MYSQL_STMT* stmt,
                          my_ulonglong exp_i,
                          my_ulonglong exp_rows[])
{
    my_ulonglong* rows = new my_ulonglong[30];
    my_ulonglong i;

    Test->set_timeout(30);
    execute_stmt_num_of_rows(stmt, rows, &i);

    Test->tprintf("Result sets number is %llu\n", i);

    if (i != exp_i)
    {
        Test->add_result(1, "Number of result sets is %llu instead of %llu\n", i, exp_i);
        return 1;
    }

    for (my_ulonglong j = 0; j < i; j++)
    {
        Test->tprintf("For result set %llu number of rows is %llu\n", j, rows[j]);
        if (rows[j] != exp_rows[j])
        {
            Test->add_result(1,
                             "For result set %llu number of rows is %llu instead of %llu\n",
                             j,
                             rows[j],
                             exp_rows[j]);
            return 1;
        }
    }
    return 0;
}

/**
 * @brief err_check Print mysql_error() and mysql_errno and compare mysql_errno with given expected value
 * @param Test TestConnections object
 * @param expected_err Expected error code
 */
void err_check(TestConnections* Test, unsigned int expected_err)
{
    Test->tprintf("Error text '%s'' error code %d\n",
                  mysql_error(Test->maxscales->conn_rwsplit[0]),
                  mysql_errno(Test->maxscales->conn_rwsplit[0]));
    if (mysql_errno(Test->maxscales->conn_rwsplit[0]) != expected_err)
    {
        Test->add_result(1,
                         "Error code is not %d, it is %d\n",
                         expected_err,
                         mysql_errno(Test->maxscales->conn_rwsplit[0]));
    }
}

int main(int argc, char* argv[])
{

    my_ulonglong* exp_rows = new my_ulonglong[30];
    MYSQL_STMT* stmt;

    TestConnections* Test = new TestConnections(argc, argv);
    Test->set_timeout(30);
    Test->maxscales->connect_rwsplit(0);

    create_t1(Test->maxscales->conn_rwsplit[0]);
    insert_into_t1(Test->maxscales->conn_rwsplit[0], 1);
    Test->stop_timeout();
    Test->repl->sync_slaves();

    Test->tprintf("**** Test 1 ****\n");


    exp_rows[0] = 16;
    compare_expected(Test, (char*) "select * from t1", 1, exp_rows);

    exp_rows[0] = 16;
    compare_expected(Test, (char*) "select * from t1 where fl=0", 1, exp_rows);

    exp_rows[0] = 10;
    compare_expected(Test, (char*) "select * from t1 limit 10", 1, exp_rows);

    Test->set_timeout(60);
    create_t1(Test->maxscales->conn_rwsplit[0]);
    insert_into_t1(Test->maxscales->conn_rwsplit[0], 3);
    Test->stop_timeout();
    Test->repl->sync_slaves();


    Test->tprintf("**** Test 2 ****\n");
    exp_rows[0] = 0;
    compare_expected(Test, (char*) "select * from t1", 1, exp_rows);

    exp_rows[0] = 16;
    compare_expected(Test, (char*) "select * from t1 where fl=0", 1, exp_rows);

    exp_rows[0] = 10;
    compare_expected(Test, (char*) "select * from t1 limit 10", 1, exp_rows);

    Test->tprintf("**** Test 3 ****\n");
    exp_rows[0] = 2;
    exp_rows[1] = 0;
    Test->try_query(Test->maxscales->conn_rwsplit[0], "DROP PROCEDURE IF EXISTS multi");
    Test->try_query(Test->maxscales->conn_rwsplit[0], "%s", test03_sql);
    compare_expected(Test, "CALL multi()", 2, exp_rows);

    Test->tprintf("**** Test 4 ****\n");
    exp_rows[0] = 1;
    exp_rows[1] = 2;
    exp_rows[2] = 1;
    exp_rows[3] = 0;
    Test->try_query(Test->maxscales->conn_rwsplit[0], "DROP PROCEDURE IF EXISTS multi");
    Test->try_query(Test->maxscales->conn_rwsplit[0], "%s", test04_sql);
    compare_expected(Test, "CALL multi()", 4, exp_rows);

    Test->tprintf("**** Test 5 ****\n");
    exp_rows[0] = 1;
    exp_rows[1] = 8;
    exp_rows[2] = 1;
    exp_rows[3] = 1;
    exp_rows[4] = 0;
    Test->try_query(Test->maxscales->conn_rwsplit[0], "DROP PROCEDURE IF EXISTS multi");
    Test->try_query(Test->maxscales->conn_rwsplit[0], "%s", test05_sql);
    compare_expected(Test, "CALL multi()", 5, exp_rows);

    Test->tprintf("**** Test 6 ****\n");
    exp_rows[0] = 0;

    Test->try_query(Test->maxscales->conn_rwsplit[0], "DROP PROCEDURE IF EXISTS multi");
    Test->try_query(Test->maxscales->conn_rwsplit[0], "%s", test06_sql);
    compare_expected(Test, "CALL multi()", 1, exp_rows);


    Test->tprintf("LONGBLOB: Trying send data via RWSplit\n");
    Test->try_query(Test->maxscales->conn_rwsplit[0], "SET GLOBAL max_allowed_packet=10000000000");
    Test->stop_timeout();
    Test->repl->connect();
    // test_longblob(Test, Test->maxscales->conn_rwsplit[0], (char *) "LONGBLOB", 512 * 1024 / sizeof(long
    // int), 17 * 2, 25);
    test_longblob(Test, Test->repl->nodes[0], (char*) "LONGBLOB", 512 * 1024 / sizeof(long int), 17 * 2, 5);
    Test->repl->close_connections();


    Test->tprintf("**** Test 7 ****\n");
    exp_rows[0] = 1;
    exp_rows[1] = 1;
    exp_rows[2] = 4;
    exp_rows[3] = 1;
    exp_rows[4] = 5;
    exp_rows[5] = 5;
    exp_rows[6] = 1;
    exp_rows[7] = 0;

    Test->try_query(Test->maxscales->conn_rwsplit[0], "DROP PROCEDURE IF EXISTS multi");
    Test->try_query(Test->maxscales->conn_rwsplit[0], "%s", test07_sql);
    compare_expected(Test, "CALL multi()", 8, exp_rows);

    Test->tprintf("**** Test 8 ****\n");
    exp_rows[0] = 0;

    Test->try_query(Test->maxscales->conn_rwsplit[0], "DROP PROCEDURE IF EXISTS multi");
    Test->try_query(Test->maxscales->conn_rwsplit[0], "%s", test08_sql);
    compare_expected(Test, "CALL multi()", 1, exp_rows);

    Test->tprintf("**** Test 9 ****\n");
    exp_rows[0] = 0;

    compare_expected(Test, "SELECT * FROM dual", 0, exp_rows);
    err_check(Test, 1096);

    Test->tprintf("**** Test 10 ****\n");
    exp_rows[0] = 1;
    exp_rows[1] = 4;

    Test->try_query(Test->maxscales->conn_rwsplit[0], "DROP PROCEDURE IF EXISTS multi");
    Test->try_query(Test->maxscales->conn_rwsplit[0], "%s", test10_sql);
    compare_expected(Test, "CALL multi()", 2, exp_rows);

    err_check(Test, 1096);

    Test->tprintf("**** Test 11 ****\n");
    exp_rows[0] = 0;

    compare_expected(Test, "SET @a=4;", 1, exp_rows);
    err_check(Test, 0);

    // Prepared statements

    Test->tprintf("**** Test 12 (C++) ****\n");
    exp_rows[0] = 0;

    stmt = mysql_stmt_init(Test->maxscales->conn_rwsplit[0]);
    if (stmt == NULL)
    {
        Test->add_result(1, "stmt init error: %s\n", mysql_stmt_error(stmt));
    }
    char* stmt1 = (char*) "SELECT * FROM t1";
    Test->add_result(mysql_stmt_prepare(stmt, stmt1, strlen(stmt1)),
                     "Error preparing stmt: %s\n",
                     mysql_stmt_error(stmt));

    compare_stmt_expected(Test, stmt, 1, exp_rows);

    mysql_stmt_close(stmt);



    Test->tprintf("**** Test 12 (MariaDB command line client) ****\n");
    exp_rows[0] = 0;
    Test->try_query(Test->maxscales->conn_rwsplit[0], "SET @table = 't1'");
    Test->try_query(Test->maxscales->conn_rwsplit[0], "SET @s = CONCAT('SELECT * FROM ', @table)");
    Test->try_query(Test->maxscales->conn_rwsplit[0], "PREPARE stmt1 FROM @s");
    compare_expected(Test, "EXECUTE stmt1", 1, exp_rows);
    Test->try_query(Test->maxscales->conn_rwsplit[0], "DEALLOCATE PREPARE stmt1");


    Test->tprintf("**** Test 13 (C++)****\n");
    exp_rows[0] = 10;
    exp_rows[1] = 0;
    stmt = mysql_stmt_init(Test->maxscales->conn_rwsplit[0]);
    if (stmt == NULL)
    {
        Test->add_result(1, "stmt init error: %s\n", mysql_stmt_error(stmt));
    }
    char* stmt2 = (char*) "SELECT * FROM t1 LIMIT 10";
    Test->add_result(mysql_stmt_prepare(stmt, stmt2, strlen(stmt2)),
                     "Error preparing stmt: %s\n",
                     mysql_stmt_error(stmt));
    compare_stmt_expected(Test, stmt, 1, exp_rows);
    mysql_stmt_close(stmt);

    Test->tprintf("**** Test 13 (MariaDB command line client) ****\n");
    Test->try_query(Test->maxscales->conn_rwsplit[0], "SET @table = 't1'");
    Test->try_query(Test->maxscales->conn_rwsplit[0],
                    "SET @s = CONCAT('SELECT * FROM ', @table,  ' LIMIT 10')");
    Test->try_query(Test->maxscales->conn_rwsplit[0], "PREPARE stmt1 FROM @s");
    compare_expected(Test, "EXECUTE stmt1", 1, exp_rows);
    Test->try_query(Test->maxscales->conn_rwsplit[0], "DEALLOCATE PREPARE stmt1");

    Test->tprintf("**** Test 14 ****\n");
    exp_rows[0] = 1;
    exp_rows[1] = 18;
    exp_rows[2] = 1;
    exp_rows[3] = 0;
    Test->try_query(Test->maxscales->conn_rwsplit[0], "DROP PROCEDURE IF EXISTS multi");
    Test->try_query(Test->maxscales->conn_rwsplit[0], "%s", test14_sql);
    compare_expected(Test, "CALL multi()", 4, exp_rows);

    Test->tprintf("**** Test 15 ****\n");
    exp_rows[0] = 0;
    Test->try_query(Test->maxscales->conn_rwsplit[0], "DROP PROCEDURE IF EXISTS multi");
    Test->try_query(Test->maxscales->conn_rwsplit[0], "%s", test15_sql);
    compare_expected(Test, "CALL multi()", 1, exp_rows);

    Test->tprintf("**** Test 16 ****\n");
    exp_rows[0] = 1;
    exp_rows[1] = 0;
    compare_expected(Test, "SELECT '' as 'A' limit 1;", 1, exp_rows);

    Test->tprintf("**** Test 17 ****\n");
    exp_rows[0] = 1;
    exp_rows[1] = 1;
    exp_rows[2] = 1;
    exp_rows[3] = 0;
    Test->try_query(Test->maxscales->conn_rwsplit[0], "DROP PROCEDURE IF EXISTS multi");
    Test->try_query(Test->maxscales->conn_rwsplit[0], "%s", test17_sql);
    compare_expected(Test, "CALL multi()", 4, exp_rows);

    Test->tprintf("**** Test 18 ****\n");
    exp_rows[0] = 1;
    exp_rows[1] = 1;
    exp_rows[2] = 1;
    exp_rows[3] = 1;
    exp_rows[4] = 1;
    exp_rows[5] = 1;
    exp_rows[6] = 1;
    exp_rows[7] = 1;
    exp_rows[8] = 1;
    exp_rows[9] = 1;
    exp_rows[10] = 1;
    exp_rows[11] = 1;
    exp_rows[12] = 1;
    exp_rows[13] = 1;
    exp_rows[14] = 1;
    exp_rows[15] = 1;
    exp_rows[16] = 1;
    exp_rows[17] = 1;
    exp_rows[18] = 1;
    exp_rows[19] = 1;
    exp_rows[20] = 0;
    Test->try_query(Test->maxscales->conn_rwsplit[0], "DROP PROCEDURE IF EXISTS multi");
    Test->try_query(Test->maxscales->conn_rwsplit[0], "%s", test18_sql);
    compare_expected(Test, "CALL multi()", 21, exp_rows);

    Test->tprintf("**** Test 19 ****\n");
    exp_rows[0] = 0;

    Test->try_query(Test->maxscales->conn_rwsplit[0], "DROP PROCEDURE IF EXISTS multi");
    Test->try_query(Test->maxscales->conn_rwsplit[0], "%s", test19_sql);
    compare_expected(Test, "CALL multi()", 1, exp_rows);

    Test->tprintf("**** Test 20 ****\n");
    exp_rows[0] = 2;
    exp_rows[1] = 0;
    Test->try_query(Test->maxscales->conn_rwsplit[0], "SET GLOBAL max_allowed_packet=10000000000");
    compare_expected(Test, "SELECT * FROM long_blob_table limit 2;", 1, exp_rows);
    err_check(Test, 0);

    Test->maxscales->close_rwsplit(0);

    Test->maxscales->ssh_node(0,
                              "sed -i \"s/max_resultset_size=900000000/max_resultset_size=9000000/\" /etc/maxscale.cnf",
                              true);
    Test->set_timeout(100);
    Test->maxscales->restart_maxscale(0);

    Test->maxscales->connect_rwsplit(0);

    Test->tprintf("**** Test 21 ****\n");
    exp_rows[0] = 0;
    Test->try_query(Test->maxscales->conn_rwsplit[0], "SET GLOBAL max_allowed_packet=10000000000");
    compare_expected(Test, "SELECT * FROM long_blob_table limit 1;", 1, exp_rows);

    Test->check_maxscale_alive(0);
    int rval = Test->global_result;
    delete Test;

    return rval;
}
