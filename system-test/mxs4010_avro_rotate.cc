/*
 * Copyright (c) 2021 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxtest/testconnections.hh>

void sync_avro(TestConnections& test, Connection& c)
{
    for (int i = 0; i < 30; i++)
    {
        auto pos = c.field("SELECT @@gtid_current_pos");
        auto res = test.maxctrl("api get services/avro-service data.attributes.router_diagnostics.gtid");

        if (res.output.find(pos) != std::string::npos)
        {
            break;
        }
        else
        {
            sleep(1);
        }
    }
}

void check_file_count(TestConnections& test, std::string count)
{
    auto res = test.maxscale->ssh_output("ls -1 /tmp/test.t1.*.avro|wc -l").output;
    test.expect(res == count, "/tmp/ should have %s Avro file(s) but it has: %s",
                count.c_str(), res.c_str());
}

int main(int argc, char* argv[])
{
    TestConnections test(argc, argv);
    TestConnections::skip_maxscale_start(true);
    test.maxscale->ssh_node("rm -f /tmp/test.t1.*.av* /tmp/current_gtid.txt", true);

    auto c = test.repl->get_connection(0);
    c.connect();
    c.query("RESET MASTER");
    c.query("CREATE OR REPLACE TABLE test.t1(id int)");
    c.query("INSERT INTO test.t1 VALUES (1)");

    test.maxscale->start();
    sync_avro(test, c);
    check_file_count(test, "1");

    test.tprintf("Insert enough rows to exceed the file size limit: file should be automatically rotated");

    for (int i = 0; i < 45; i++)
    {
        c.query("INSERT INTO test.t1 SELECT seq FROM seq_0_to_256");
    }

    sync_avro(test, c);
    check_file_count(test, "2");

    test.tprintf("Call the rotate command and insert a row. The third Avro file should be created");

    test.check_maxctrl("call command avrorouter rotate avro-service");
    c.query("INSERT INTO test.t1 SELECT seq FROM seq_0_to_256");
    sync_avro(test, c);
    check_file_count(test, "3");

    test.tprintf("Enable file pruning based on data age: only the latest file should exist");
    test.maxscale->ssh_node("sed -i 's/max_data_age=10min/max_data_age=1s/' /etc/maxscale.cnf", true);
    test.maxscale->restart();

    test.check_maxctrl("call command avrorouter rotate avro-service");

    // Wait for a while to make sure the data is old enough
    sleep(3);

    c.query("INSERT INTO test.t1 SELECT seq FROM seq_0_to_256");
    sync_avro(test, c);

    check_file_count(test, "1");

    c.query("DROP TABLE test.t1(id int)");
    test.maxscale->ssh_node("rm -f /tmp/test.t1.*.av* /tmp/current_gtid.txt", true);

    return test.global_result;
}
