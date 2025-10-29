/*
 * Copyright (c) 2025 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-09-12
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxtest/testconnections.hh>
#include <maxtest/kafka.hh>

using namespace std::literals::string_literals;

void read_messages(TestConnections& test, Consumer& consumer, int n_expected)
{
    int i = consumer.try_consume_messages(n_expected);
    test.expect(i == n_expected, "Expected %d messages, got %d", n_expected, i);
}

int main(int argc, char** argv)
{
    // The table with forward slashes can't be serialized as it get confused with a directory path.
    std::array table_names = {
        "special-table-with-dashes",
        "special/table/with/forward/slashes",
        "special table with spaces",
        "special table, special characters!?",
        "\"double quoted table\"",
        "'single quoted one'",
        "\\this one starts with a backspace",
        "(╯°□°)╯︵ ┻━┻",
    };

    TestConnections::skip_maxscale_start(true);
    TestConnections test(argc, argv);
    Kafka kafka(test);
    kafka.create_topic("test.t1");
    Consumer consumer(test, "kafkacdc");

    test.tprintf("Inserting data");
    auto conn = test.repl->get_connection(0);
    conn.connect();

    for (std::string table : table_names)
    {
        conn.query("CREATE TABLE `" + table + "`(id INT)");
    }

    auto gtid = conn.field("SELECT @@gtid_binlog_pos");
    test.tprintf("Start GTID: %s", gtid.c_str());
    test.maxscale->ssh_output("sed -i \"s/REPLACETHIS/" + gtid + "/\" /etc/maxscale.cnf", true);

    int i = 0;
    for (std::string table : table_names)
    {
        conn.query("INSERT INTO `" + table + "` VALUES (" + std::to_string(i++) + ")");
    }

    test.tprintf("Give MaxScale some time to process the events");
    test.maxscale->start();
    sleep(5);

    read_messages(test, consumer, i);

    test.tprintf("Restart MaxScale and insert more data");
    test.maxscale->restart();

    for (std::string table : table_names)
    {
        conn.query("INSERT INTO `" + table + "` VALUES (" + std::to_string(i++) + ")");
    }

    sleep(5);

    read_messages(test, consumer, i);

    for (std::string table : table_names)
    {
        conn.query("DROP TABLE `" + table + "`");
    }

    return test.global_result;
}
