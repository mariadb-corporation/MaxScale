/**
 * @file setup_binlog.cpp test of simple binlog router setup
 * - setup one master, one slave directly connected to real master and two slaves connected to binlog router
 * - create table and put data into it using connection to master
 * - check data using direct commection to all backend
 * - compare sha1 checksum of binlog file on master and on Maxscale machine
 * - START TRANSACTION
 * - SET autocommit = 0
 * - INSERT INTO t1 VALUES(111, 10)
 * - check SELECT * FROM t1 WHERE fl=10 - expect one row x=111
 * - ROLLBACK
 * - INSERT INTO t1 VALUES(112, 10)
 * - check SELECT * FROM t1 WHERE fl=10 - expect one row x=112 and no row with x=111
 * - DELETE FROM t1 WHERE fl=10
 * - START TRANSACTION
 * - INSERT INTO t1 VALUES(111, 10)
 * - check SELECT * FROM t1 WHERE fl=10 - expect one row x=111 from master and slave
 * - DELETE FROM t1 WHERE fl=10
 * - compare sha1 checksum of binlog file on master and on Maxscale machine
 * - Re-create t1 table via master
 * - STOP SLAVE against Maxscale binlog
 * - put data to t1
 * - START SLAVE against Maxscale binlog
 * - wait to let replication happens
 * - check data on all nodes
 * - chack sha1
 * - repeat last test with FLUSH LOGS on master 1. before putting data to Master 2. after putting data to master
 */


#include <iostream>
#include "testconnections.h"
#include "maxadmin_operations.h"
#include "sql_t1.h"

#include "test_binlog_fnc.h"


int main(int argc, char *argv[])
{
    TestConnections test(argc, argv);

    test.binlog_cmd_option = 1;
    test.start_binlog();
    test_binlog(&test);

    test.check_log_err("SET NAMES utf8mb4", false);
    test.check_log_err("set autocommit=1", false);
    test.check_log_err("select USER()", false);

    return test.global_result;
}
