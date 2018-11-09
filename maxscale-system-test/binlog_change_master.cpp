/**
 * @file binlog_change_master.cpp In the binlog router setup stop Master and promote one of the Slaves to be
 * new Master
 * - setup binlog
 * - start thread wich executes transactions
 * - block master
 * - transaction thread tries to elect a new master a continue with new master
 * - continue transaction with new master
 * - stop transactions
 * - wait
 * - check data on all nodes
 */

#include "testconnections.h"
#include "binlog_change_master_common.cpp"

int main(int argc, char* argv[])
{
    TestConnections test(argc, argv);

    auto cb = [&](MYSQL* blr) {

            // Get the name of the current binlog
            std::string file = get_row(test.repl->nodes[0], "SHOW MASTER STATUS")[0];
            std::string target = get_row(test.repl->nodes[2], "SHOW MASTER STATUS")[0];

            // Flush logs until the candidate master has a higher binlog sequence number
            while (target.back() <= file.back())
            {
                execute_query(test.repl->nodes[2], "FLUSH LOGS");
                target = get_row(test.repl->nodes[2], "SHOW MASTER STATUS")[0];
            }

            // Promote the candidate master by pointing the binlogrouter at it

            test.try_query(blr, "STOP SLAVE");
            test.try_query(blr, "CHANGE MASTER TO MASTER_HOST='%s', MASTER_PORT=%d,"
                                "MASTER_LOG_FILE='%s', MASTER_LOG_POS=4",
                           test.repl->IP[2], test.repl->port[2], target.c_str());
            test.try_query(blr, "START SLAVE");
        };

    run_test(test, cb);

    return test.global_result;
}
