/**
 * The GTID version of binlog_change_master
 */

#include "testconnections.h"
#include "binlog_change_master_common.cpp"

int main(int argc, char* argv[])
{
    TestConnections test(argc, argv);
    test.binlog_master_gtid = true;
    test.binlog_slave_gtid = true;

    auto cb = [&](MYSQL* blr) {
            test.try_query(blr, "STOP SLAVE");
            test.try_query(blr, "CHANGE MASTER TO MASTER_HOST='%s', MASTER_PORT=%d,"
                                "MASTER_USE_GTID=SLAVE_POS",
                           test.repl->IP[2], test.repl->port[2]);
            test.try_query(blr, "START SLAVE");
        };

    run_test(test, cb);

    return test.global_result;
}
