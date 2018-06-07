/**
 * MXS-1542: https://jira.mariadb.org/browse/MXS-1542
 *
 * Check that UTF16 strings work.
 */

#include "testconnections.h"

int main(int argc, char** argv)
{
    TestConnections::skip_maxscale_start(true);
    TestConnections::check_nodes(false);
    TestConnections test(argc, argv);

    test.replicate_from_master();

    test.repl->connect();
    execute_query(test.repl->nodes[0],
                  "CREATE OR REPLACE TABLE t1 (data varchar(30) NOT NULL) DEFAULT CHARSET=utf16");
    execute_query(test.repl->nodes[0],
                  "INSERT INTO t1 VALUES ('Hello World'), ('Բարեւ աշխարհ'), ('こんにちは世界'), ('你好，世界'), ('Привет мир')");

    // Wait for the data to be processed
    const char* logmsg = "Waiting until more data is written";
    test.maxscales->ssh_node_f(0, true,
                             "for ((i=0;i<15;i++)); do grep '%s' /var/log/maxscale/maxscale.log && break || sleep 1; done", logmsg);

    // Check if the Avro file contains the inserted value
    int rc = test.maxscales->ssh_node_f(0, true,
                                      "maxavrocheck -d /var/lib/maxscale/avro/test.t1.000001.avro|grep 'Hello World'");
    test.add_result(rc == 0, "Data is converted when a failure to convert is expected");

    printf("\n"
           "o-------------------------------------------------------------------o\n"
           "|The test is expected to fail, change it when the MXS-1542 is fixed.|\n"
           "o-------------------------------------------------------------------o\n"
           "\n");

    test.revert_replicate_from_master();
    return test.global_result;
}
