/*
 *  Copyright (c) 2024 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-09-09
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxtest/testconnections.hh>
#include <maxtest/kafka.hh>

using std::string;

namespace
{
void test_main(TestConnections& test)
{
    Kafka kafka(test);
    kafka.create_topic("test.t1");

    auto& mxs = *test.maxscale;
    mxs.start_and_check_started();

    if (test.ok())
    {
        const char req_sec_transport[] = "require_secure_transport";
        test.tprintf("MaxScale started with %s=1.", req_sec_transport);

        auto test_forbidden_cmd = [&](const string& cmd) {
            auto res = mxs.maxctrl(cmd);
            test.expect(res.rc != 0, "'maxctrl %s' succeeded when it should have failed.", cmd.c_str());
            test.expect(res.output.find(req_sec_transport) != string::npos,
                        "Error message does not include 'require_secure_transport'.");
            test.tprintf("Command output: %s", res.output.c_str());
        };

        test.tprintf("Attempting to disable ssl from a listener.");
        test_forbidden_cmd("--secure -n false alter listener  RW-Split-Listener ssl false");

        test.tprintf("Attempting to disable ssl from a server.");
        test_forbidden_cmd("--secure -n false alter server server2 ssl false");

        test.tprintf("Attempting to disable ssl from REST-API.");
        test_forbidden_cmd("--secure -n false alter maxscale admin_ssl_cert=\"\" admin_ssl_key=\"\"");

        test.tprintf("Attempting to disable ssl from Kafka.");
        test_forbidden_cmd("--secure -n false alter service Kafka-Importer kafka_ssl=false");

        test.tprintf("Attempting to create a server without SSL.");
        test_forbidden_cmd("--secure -n false create server MyServer 127.0.0.1 3306");
    }
}
}

int main(int argc, char* argv[])
{
    TestConnections test;
    // TestConnections doesn't handle REST-API SSL.
    TestConnections::skip_maxscale_start(true);
    return test.run_test(argc, argv, test_main);
}
