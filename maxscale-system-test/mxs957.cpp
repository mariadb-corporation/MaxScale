/**
 * @file mxs957.cpp Execute given SQL through readwritesplit (with temporary tables usage)
 *
 *
 * Execute the following SQL through readwritesplit without errors.
 *
 * CREATE OR REPLACE TABLE t1(`id` INT(10) UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY);
 * CREATE OR REPLACE TABLE t2(`id` INT(10) UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY);
 * CREATE TEMPORARY TABLE temp1(`id` INT(10) UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY);
 * INSERT INTO temp1 values (1), (2), (3);
 * INSERT INTO t1 values (1), (2), (3);
 * INSERT INTO t2 values (1), (2), (3);
 * CREATE TEMPORARY TABLE temp2
 *        SELECT DISTINCT p.id FROM temp1 p JOIN t1 t ON (t.id = p.id)
 *        LEFT JOIN t2 ON (t.id = t2.id)
 *        WHERE p.id IS NOT NULL AND @@server_id IS NOT NULL;
 * SELECT * FROM temp2;
 */


#include <iostream>
#include <unistd.h>
#include "testconnections.h"

using namespace std;

const char* queries[] =
{
    "USE test",
    "CREATE OR REPLACE TABLE t1(`id` INT(10) UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY)",
    "CREATE OR REPLACE TABLE t2(`id` INT(10) UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY)",
    "CREATE TEMPORARY TABLE temp1(`id` INT(10) UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY)",
    "INSERT INTO temp1 values (1), (2), (3)",
    "INSERT INTO t1 values (1), (2), (3)",
    "INSERT INTO t2 values (1), (2), (3)",
    "CREATE TEMPORARY TABLE temp2 SELECT DISTINCT p.id FROM temp1 p JOIN t1 t ON (t.id = p.id) LEFT JOIN t2 ON (t.id = t2.id) WHERE p.id IS NOT NULL AND @@server_id IS NOT NULL",
    "SELECT * FROM temp2",
    "DROP TABLE t1",
    "DROP TABLE t2",
    NULL
};

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);
    Test->connect_maxscale();

    for (int i = 0; queries[i]; i++)
    {
        Test->set_timeout(30);
        Test->try_query(Test->conn_rwsplit, queries[i]);
    }
    int rval = Test->global_result;
    delete Test;
    return rval;
}
