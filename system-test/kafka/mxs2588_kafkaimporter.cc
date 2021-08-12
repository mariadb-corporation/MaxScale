#include <maxtest/testconnections.hh>
#include <maxtest/kafka.hh>

bool read_rows(TestConnections& test, std::string table, int num_msg)
{
    bool ok = false;
    auto conn = test.repl->get_connection(0);
    test.expect(conn.connect(), "Connection to master failed: %s", conn.error());

    for (int i = 0; i < 10 && !ok; i++)
    {
        std::string err;
        auto result = conn.rows("SELECT id, data FROM " + table);

        if (!result.empty())
        {
            int i = 0;

            for (auto row : result)
            {
                auto expected = std::to_string(i);

                if (expected != row[0])
                {
                    err = "Expected " + expected + ", got " + row[0] + " (" + row[1] + ")";
                    break;
                }

                ++i;
            }

            if (i == num_msg)
            {
                ok = true;
            }
            else
            {
                err = "Not enough rows";
            }
        }
        else
        {
            err = "Got empty result";
        }

        if (!ok)
        {
            test.tprintf("Round %d: %s", i + 1, err.c_str());
            sleep(5);
        }
        else
        {
            test.tprintf("Round %d: all rows found", i + 1);
        }
    }

    return ok;
}

void test_table_in_topic(TestConnections& test)
{
    auto conn = test.repl->get_connection(0);
    test.expect(conn.connect(), "Connection to master failed: %s", conn.error());
    conn.query("DROP TABLE IF EXISTS test.t1");

    test.tprintf("Producing 100 messages");
    Producer producer(test);
    const int NUM_MSG = 100;

    for (int i = 0; i < NUM_MSG; i++)
    {
        producer.produce_message("test.t1", "some key, should be ignored",
                                 "{\"_id\": " + std::to_string(i) + ", \"data\": \"hello world\"}");
    }

    test.tprintf("Flush messages");
    producer.flush();

    test.expect(read_rows(test, "t1", NUM_MSG), "Failed to read rows");
    conn.query("DROP TABLE test.t1");
}

void test_table_in_key(TestConnections& test)
{
    auto conn = test.repl->get_connection(0);
    test.expect(conn.connect(), "Connection to master failed: %s", conn.error());
    conn.query("DROP TABLE IF EXISTS test.t2");

    test.check_maxctrl("alter service Kafka-Importer topics second_topic table_name_in key");

    test.tprintf("Producing 100 messages");
    Producer producer(test);
    const int NUM_MSG = 100;

    for (int i = 0; i < NUM_MSG; i++)
    {
        producer.produce_message("second_topic", "test.t2",
                                 "{\"_id\": " + std::to_string(i) + ", \"data\": \"hello world\"}");
    }

    test.tprintf("Flush messages");
    producer.flush();

    test.expect(read_rows(test, "t2", NUM_MSG), "Failed to read rows");

    test.check_maxctrl("alter service Kafka-Importer batch_size 1");

    test.tprintf("Producing a message with a table name that must be escaped");
    producer.produce_message("second_topic", "test.`that's-a-bad-name`",
                             "{\"_id\": 0, \"data\": \"this should work\"}");
    producer.flush();

    test.expect(read_rows(test, "`that's-a-bad-name`", 1), "Failed to read rows");


    test.tprintf("Producing a message with a table name that has spaces in it");
    producer.produce_message("second_topic", "`test`.`spaces in table name`",
                             "{\"_id\": 0, \"data\": \"this should also work\"}");
    producer.flush();

    test.expect(read_rows(test, "`spaces in table name`", 1), "Failed to read rows");

    conn.query("DROP TABLE test.t2");
    conn.query("DROP TABLE test.`that's-a-bad-name`");
    conn.query("DROP TABLE test.`spaces in table name`");
}

int main(int argc, char** argv)
{
    TestConnections::skip_maxscale_start(true);
    TestConnections test(argc, argv);
    Kafka kafka(test);
    kafka.create_topic("test.t1");
    kafka.create_topic("second_topic");
    test.maxscale->start();

    test_table_in_topic(test);
    test_table_in_key(test);

    return test.global_result;
}
