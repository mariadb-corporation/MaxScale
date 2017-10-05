/**
 * @file setup_binlog_gtid.cpp - Basic GTID testing of binlogrouter
 */

#include "testconnections.h"
#include "test_binlog_fnc.h"

int main(int argc, char *argv[])
{

    TestConnections test(argc, argv);
    test.binlog_master_gtid = true;
    test.binlog_slave_gtid = true;

    test.start_binlog();
    test_binlog(&test);

    test.check_log_err("SET NAMES utf8mb4", false);
    test.check_log_err("set autocommit=1", false);
    test.check_log_err("select USER()", false);

    return test.global_result;
}
