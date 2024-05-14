/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-04-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxtest/testconnections.hh>
#include <maxtest/kafka.hh>
#include <maxbase/json.hh>
#include <maxbase/string.hh>

using namespace std::literals::string_literals;

mxb::Json get_json(TestConnections& test, Consumer& consumer)
{
    mxb::Json js;

    if (auto msg = consumer.consume_one_message())
    {
        test.expect(js.load_string(std::string((const char*)msg->payload(), msg->len())),
                    "Failed to read JSON from message: %s", js.error_msg().c_str());
    }
    else
    {
        test.expect(false, "Failed to consume message.");
    }

    return js;
}

int get_sequence(std::string str)
{
    auto tok = mxb::strtok(str, "-");
    return tok.size() == 3 ? atoi(tok[2].c_str()) : 0;
}

void read_messages(TestConnections& test, Consumer& consumer, int n_expected, int seq_start, int seq_end)
{
    for (int i = 0; i < n_expected; i++)
    {
        if (auto js = get_json(test, consumer))
        {
            int seq = js.get_int("sequence");

            if (js.get_string("namespace") == "MaxScaleChangeDataSchema.avro")
            {
                auto gtid = js.get_string("gtid");
                seq = get_sequence(gtid);
            }

            test.expect(seq > seq_start && seq <= seq_end,
                        "Expected GTID with sequence between %d and %d, got %d: %s",
                        seq_start, seq_end, seq, js.to_string().c_str());
        }
        else
        {
            test.expect(false, "Expected %d messages, got %d", n_expected, i);
            break;
        }
    }
}

int main(int argc, char** argv)
{
    TestConnections::skip_maxscale_start(true);
    TestConnections test(argc, argv);
    Kafka kafka(test);
    kafka.create_topic("kafkacdc");

    auto conn = test.repl->get_connection(0);
    conn.connect();
    auto gtid = conn.field("SELECT @@gtid_binlog_pos");
    test.maxscale->ssh_output("sed -i -e \"/Kafka-CDC/ a gtid=" + gtid + "\" /etc/maxscale.cnf");
    test.maxscale->start();

    // Connect to Kafka
    Consumer consumer(test, "kafkacdc");

    test.log_printf("Inserting data");
    auto gtid_start = conn.field("SELECT @@gtid_binlog_pos");
    conn.query("CREATE TABLE t1(id INT)");
    conn.query("INSERT INTO t1 VALUES (1), (2), (3)");
    conn.query("UPDATE t1 SET id = 4 WHERE id = 2");
    conn.query("DELETE FROM t1 WHERE id = 3");
    auto gtid_end = conn.field("SELECT @@gtid_binlog_pos");
    test.log_printf("GTID: %s -> %s", gtid_start.c_str(), gtid_end.c_str());

    test.log_printf("Give MaxScale some time to process the events");
    sleep(5);

    read_messages(test, consumer, 7, get_sequence(gtid_start), get_sequence(gtid_end));

    gtid_start = conn.field("SELECT @@gtid_binlog_pos");
    conn.query("INSERT INTO t1 VALUES (4), (5), (6)");
    gtid_end = conn.field("SELECT @@gtid_binlog_pos");
    test.log_printf("GTID: %s -> %s", gtid_start.c_str(), gtid_end.c_str());
    sleep(5);

    read_messages(test, consumer, 3, get_sequence(gtid_start), get_sequence(gtid_end));

    test.log_printf("Restarting MaxScale and inserting data");
    test.maxscale->stop();
    test.maxscale->ssh_output("rm /var/lib/maxscale/Kafka-CDC/current_gtid.txt");
    test.maxscale->start();

    gtid_start = conn.field("SELECT @@gtid_binlog_pos");
    conn.query("INSERT INTO t1 VALUES (7), (8), (9)");
    gtid_end = conn.field("SELECT @@gtid_binlog_pos");
    test.log_printf("GTID: %s -> %s", gtid_start.c_str(), gtid_end.c_str());
    sleep(5);

    read_messages(test, consumer, 3, get_sequence(gtid_start), get_sequence(gtid_end));

    test.log_printf("Enable match and exclude");
    test.maxscale->stop();
    gtid_start = conn.field("SELECT @@gtid_binlog_pos");
    test.maxscale->ssh_output("sed -i -e \"$ a match=cat\" -e \"$ a exclude=bob\" /etc/maxscale.cnf", true);
    conn.query("CREATE TABLE bob(id INT)");
    conn.query("INSERT INTO bob VALUES (10)");
    conn.query("CREATE TABLE bobcat(id INT)");
    conn.query("INSERT INTO bobcat VALUES (11)");
    conn.query("CREATE TABLE cat(id INT)");
    conn.query("INSERT INTO cat VALUES (12)");
    gtid_end = conn.field("SELECT @@gtid_binlog_pos");
    test.log_printf("GTID: %s -> %s", gtid_start.c_str(), gtid_end.c_str());

    test.maxscale->start();
    sleep(5);

    auto js = get_json(test, consumer);
    test.expect(js.get_string("table") == "cat",
                "Expected schema event: %s", js.to_string().c_str());

    js = get_json(test, consumer);
    test.expect(js.get_string("table_name") == "cat",
                "Expected data event: %s", js.to_string().c_str());
    test.expect(js.get_int("id") == 12,
                "Expected data to be 12: %s", js.to_string().c_str());

    conn.query("DROP TABLE bob");
    conn.query("DROP TABLE bobcat");
    conn.query("DROP TABLE cat");
    conn.query("DROP TABLE t1");

    return test.global_result;
}
