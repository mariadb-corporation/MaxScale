/**
 * @file avro_alter.cpp Test ALTER TABLE handling of avrorouter
 */

#include "testconnections.h"
#include <jansson.h>
#include <sstream>
#include <iostream>

int main(int argc, char *argv[])
{

    TestConnections test(argc, argv);
    test.set_timeout(600);
    test.ssh_maxscale(true, (char *) "rm -rf /var/lib/maxscale/avro");

    /** Start master to binlogrouter replication */
    if (!test.replicate_from_master())
    {
        return 1;
    }

    test.set_timeout(120);
    test.repl->connect();

    execute_query_silent(test.repl->nodes[0], "DROP TABLE test.t1");
    execute_query(test.repl->nodes[0], "CREATE TABLE test.t1(id INT)");
    execute_query(test.repl->nodes[0], "INSERT INTO test.t1 VALUES (1)");
    execute_query(test.repl->nodes[0], "ALTER TABLE test.t1 ADD COLUMN a VARCHAR(100)");
    execute_query(test.repl->nodes[0], "INSERT INTO test.t1 VALUES (2, \"a\")");
    execute_query(test.repl->nodes[0], "ALTER TABLE test.t1 ADD COLUMN b FLOAT");
    execute_query(test.repl->nodes[0], "INSERT INTO test.t1 VALUES (3, \"b\", 3.0)");
    execute_query(test.repl->nodes[0], "ALTER TABLE test.t1 CHANGE COLUMN b c DATETIME(3)");
    execute_query(test.repl->nodes[0], "INSERT INTO test.t1 VALUES (4, \"c\", NOW())");
    execute_query(test.repl->nodes[0], "ALTER TABLE test.t1 DROP COLUMN c");
    execute_query(test.repl->nodes[0], "INSERT INTO test.t1 VALUES (5, \"d\")");

    test.repl->close_connections();

    /** Give avrorouter some time to process the events */
    test.stop_timeout();
    sleep(10);
    test.set_timeout(120);

    for (int i = 1; i <=5; i++)
    {
        std::stringstream cmd;
        cmd << "maxavrocheck -d /var/lib/maxscale/avro/test.t1.00000" << i << ".avro";
        char* rows = test.ssh_maxscale_output(true, cmd.str().c_str());
        int nrows = 0;
        std::istringstream iss;
        iss.str(rows);

        for (std::string line; std::getline(iss, line);)
        {
            json_error_t err;
            json_t* json = json_loads(line.c_str(), 0, &err);
            test.tprintf("%s", line.c_str());
            test.add_result(json == NULL, "Failed to parse JSON: %s", line.c_str());
            json_decref(json);
            nrows++;
        }

        test.add_result(nrows != 1, "Expected 1 line in file number %d, got %d: %s", i, nrows, rows);
        free(rows);
    }

    execute_query(test.repl->nodes[0], "DROP TABLE test.t1;RESET MASTER");
    test.repl->fix_replication();

    return test.global_result;
}
