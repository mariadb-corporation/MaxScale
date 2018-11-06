/**
 * MXS-2106: Maxscale CDC JSON output does not respect null values
 */

#include "testconnections.h"
#include <cdc_connector.h>

int main(int argc, char** argv)
{
    TestConnections::skip_maxscale_start(true);
    TestConnections::check_nodes(false);
    TestConnections test(argc, argv);

    test.set_timeout(120);
    test.replicate_from_master(0);

    test.repl->connect();
    execute_query(test.repl->nodes[0],
                  "CREATE OR REPLACE TABLE `test`.`test1` ("
                  "  `test1_id` int(10) unsigned NOT NULL AUTO_INCREMENT,"
                  "  `some_id` int(10) unsigned DEFAULT NULL,"
                  "  `desc` varchar(50) DEFAULT NULL,"
                  "  `some_date` timestamp NULL DEFAULT NULL,"
                  "  `updated` timestamp NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,"
                  "  PRIMARY KEY (`test1_id`)"
                  ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;"
                  "INSERT INTO test.test1(some_id,`desc`,some_date) VALUES (1,NULL,NULL), (NULL,'value1',NULL),"
                  "(NULL,NULL,NOW());"
                  "UPDATE test.test1 SET some_id = NULL, `desc` = 'value2', some_date = NOW() WHERE test1_id = 1;"
                  "UPDATE test.test1 SET some_id = 35, `desc` = NULL, some_date = NULL WHERE test1_id = 2;");

    /** Give avrorouter some time to process the events */
    test.stop_timeout();
    sleep(10);
    test.set_timeout(120);

    CDC::Connection conn(test.maxscales->IP[0], 4001, "skysql", "skysql");

    test.expect(conn.connect("test.test1"), "Failed to connect");

    auto check = [&](const std::string& name) {
            static int i = 1;
            CDC::SRow row = conn.read();

            if (row)
            {
                test.expect(row->is_null(name),
                            "%d: `%s` is not null: %s",
                            i++,
                            name.c_str(),
                            row->value(name).c_str());
            }
            else
            {
                test.tprintf("Error: %s", conn.error().c_str());
            }
        };

    // The three inserts
    check("some_date");
    check("some_id");
    check("some_id");

    // First update
    check("desc");
    check("some_id");

    // Second update
    check("some_id");
    check("desc");

    execute_query(test.repl->nodes[0], "DROP TABLE `test1`");
    test.repl->disconnect();

    return test.global_result;
}
