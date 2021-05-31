/**
 * @file avro_alter.cpp Test ALTER TABLE handling of avrorouter
 */

#include <maxtest/testconnections.hh>
#include <jansson.h>
#include <limits.h>
#include <sstream>
#include <iostream>

int main(int argc, char* argv[])
{
    TestConnections::skip_maxscale_start(true);
    TestConnections test(argc, argv);

    test.set_timeout(120);
    test.repl->connect();

    // This makes sure the binlogs don't have anything else
    execute_query(test.repl->nodes[0], "RESET MASTER");

    // Execute two events for each version of the schema
    execute_query_silent(test.repl->nodes[0], "DROP TABLE test.t1");
    execute_query(test.repl->nodes[0], "CREATE TABLE test.t1(id INT)");
    execute_query(test.repl->nodes[0], "INSERT INTO test.t1 VALUES (1)");
    execute_query(test.repl->nodes[0], "DELETE FROM test.t1");

    execute_query(test.repl->nodes[0], "ALTER TABLE test.t1 ADD COLUMN a VARCHAR(100)");
    execute_query(test.repl->nodes[0], "INSERT INTO test.t1 VALUES (2, \"a\")");
    execute_query(test.repl->nodes[0], "DELETE FROM test.t1");

    execute_query(test.repl->nodes[0], "ALTER TABLE test.t1 ADD COLUMN (b FLOAT)");
    execute_query(test.repl->nodes[0], "INSERT INTO test.t1 VALUES (3, \"b\", 3.0)");
    execute_query(test.repl->nodes[0], "DELETE FROM test.t1");

    execute_query(test.repl->nodes[0], "ALTER TABLE test.t1 CHANGE COLUMN b c DATETIME(3)");
    execute_query(test.repl->nodes[0], "INSERT INTO test.t1 VALUES (4, \"c\", NOW())");
    execute_query(test.repl->nodes[0], "DELETE FROM test.t1");

    execute_query(test.repl->nodes[0], "ALTER TABLE test.t1 MODIFY COLUMN c DATETIME(6)");
    execute_query(test.repl->nodes[0], "INSERT INTO test.t1 VALUES (4, \"c\", NOW())");
    execute_query(test.repl->nodes[0], "DELETE FROM test.t1");

    execute_query(test.repl->nodes[0], "ALTER TABLE test.t1 DROP COLUMN c");
    execute_query(test.repl->nodes[0], "INSERT INTO test.t1 VALUES (5, \"d\")");
    execute_query(test.repl->nodes[0], "DELETE FROM test.t1");

    execute_query(test.repl->nodes[0], "ALTER TABLE test.t1 ADD COLUMN c VARCHAR(100) COMMENT \"a \\\"comment\\\"\" DEFAULT 'the \\'default\\' value', ADD COLUMN d INT AFTER a, ADD COLUMN e FLOAT FIRST");
    execute_query(test.repl->nodes[0], "INSERT INTO test.t1 VALUES (6.0, 6, \"e\", 6, 'e')");
    execute_query(test.repl->nodes[0], "DELETE FROM test.t1");

    execute_query(test.repl->nodes[0], "CREATE TABLE test.t2 (a INT, b FLOAT)");
    execute_query(test.repl->nodes[0], "RENAME TABLE test.t1 TO test.t1_old, test.t2 TO test.t1");
    execute_query(test.repl->nodes[0], "DROP TABLE test.t1_old");
    execute_query(test.repl->nodes[0], "INSERT INTO test.t1 VALUES (8, 9)");
    execute_query(test.repl->nodes[0], "DELETE FROM test.t1");

    execute_query(test.repl->nodes[0], "CREATE TABLE test.t2 LIKE test.t1");
    execute_query(test.repl->nodes[0], "DROP TABLE test.t1");
    execute_query(test.repl->nodes[0], "ALTER TABLE test.t2 RENAME TO test.t1");
    execute_query(test.repl->nodes[0], "INSERT INTO test.t1 VALUES (10, 11)");
    execute_query(test.repl->nodes[0], "DELETE FROM test.t1");

    execute_query(test.repl->nodes[0], "CREATE TABLE test.t2 (LIKE test.t1)");
    execute_query(test.repl->nodes[0], "DROP TABLE test.t1");
    execute_query(test.repl->nodes[0], "ALTER TABLE test.t2 RENAME TO test.t1, DISABLE KEYS");
    execute_query(test.repl->nodes[0], "INSERT INTO test.t1 VALUES (12, 13)");
    execute_query(test.repl->nodes[0], "DELETE FROM test.t1");

    execute_query(test.repl->nodes[0], "ALTER TABLE test.t1 ADD COLUMN `g-g` VARCHAR(100) FIRST");
    execute_query(test.repl->nodes[0], "INSERT INTO test.t1 VALUES ('a', 14, 15)");
    execute_query(test.repl->nodes[0], "DELETE FROM test.t1");

    execute_query(test.repl->nodes[0], "ALTER TABLE test.t1 CHANGE COLUMN a h INT FIRST, CHANGE COLUMN b i INT AFTER h");
    execute_query(test.repl->nodes[0], "INSERT INTO test.t1 VALUES (16, 17, \"d\")");
    execute_query(test.repl->nodes[0], "DELETE FROM test.t1");

    test.maxscale->start();

    /** Give avrorouter some time to process the events */
    test.stop_timeout();
    sleep(10);
    test.set_timeout(120);

    for (int i = 1; i <= 12; i++)
    {
        char cmd[PATH_MAX];
        snprintf(cmd, sizeof(cmd), "maxavrocheck -d /var/lib/maxscale/avro/test.t1.%06d.avro", i);
        auto res = test.maxscale->ssh_output(cmd);
        int nrows = 0;
        std::istringstream iss;
        iss.str(res.output);

        for (std::string line; std::getline(iss, line);)
        {
            json_error_t err;
            json_t* json = json_loads(line.c_str(), 0, &err);
            test.tprintf("%s", line.c_str());
            test.add_result(json == NULL, "Failed to parse JSON: %s", line.c_str());
            json_decref(json);
            nrows++;
        }

        // The number of changes that are present in each version of the schema
        const int nchanges = 2;
        test.add_result(nrows != nchanges,
                        "Expected %d line in file number %d, got %d: %s",
                        nchanges, i, nrows, res.output.c_str());
    }

    test.stop_timeout();
    execute_query(test.repl->nodes[0], "DROP TABLE test.t1;RESET MASTER");
    test.repl->close_connections();

    return test.global_result;
}
