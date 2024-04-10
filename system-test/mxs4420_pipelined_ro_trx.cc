/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-04-03
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxtest/testconnections.hh>
#include <maxbase/string.hh>

void do_test(TestConnections& test)
{
    auto c = test.maxscale->rwsplit();
    test.expect(c.connect(), "Failed to connect: %s", c.error());
    c.send_query("START TRANSACTION READ ONLY");
    c.send_query("SELECT @@server_id");

    for (int i = 0; i < 50; i++)
    {
        c.send_query("SELECT @@server_id");
    }

    c.send_query("COMMIT");

    test.expect(c.read_query_result(), "START TRANSACTION READ ONLY failed: %s", c.error());
    auto server_id = c.read_query_result_field();
    test.expect(server_id.has_value() && !server_id->empty(), "Failed to read @@server_id: %s", c.error());


    for (int i = 0; i < 50 && test.ok(); i++)
    {
        auto id = c.read_query_result_field();

        if (test.expect(id.has_value(), "Failed to read query: %s", c.error()))
        {
            test.expect(id == *server_id, "Expected response from '%s' but got one from '%s'.",
                        server_id->c_str(), id->c_str());
        }
    }

    test.expect(c.read_query_result(), "COMMIT failed: %s", c.error());
}

void test_main(TestConnections& test)
{
    // First run the test with the basic configuration
    do_test(test);

    for (auto trx_replay : {"transaction_replay=false", "transaction_replay=true"})
    {
        for (auto causal_reads : {
            "causal_reads=none",
            "causal_reads=local",
            "causal_reads=fast",
            "causal_reads=global",
            "causal_reads=fast_global",
            "causal_reads=universal",
            "causal_reads=fast_universal",
        })
        {
            auto cnf = mxb::cat(trx_replay, " ", causal_reads);
            test.tprintf("Testing: %s", cnf.c_str());
            test.check_maxctrl("alter service RW-Split-Router " + cnf);
            do_test(test);
        }
    }

    std::string cnf = "transaction_replay=false causal_reads=none optimistic_trx=true";
    test.tprintf("Testing: %s", cnf.c_str());
    test.check_maxctrl("alter service RW-Split-Router " + cnf);
    do_test(test);
}

int main(int argc, char** argv)
{
    return TestConnections().run_test(argc, argv, test_main);
}
