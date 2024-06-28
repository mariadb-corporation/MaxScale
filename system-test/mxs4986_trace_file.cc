/*
 * Copyright (c) 2024 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2029-02-28
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxtest/testconnections.hh>
#include <maxbase/string.hh>

void create_load(TestConnections& test, int iterations)
{
    auto c = test.maxscale->rwsplit();
    c.connect();

    for (int i = 0; i < iterations; i++)
    {
        c.query("SELECT 1");
    }
}

void test_main(TestConnections& test)
{
    test.maxscale->stop();
    test.maxscale->ssh_node("find /tmp -name 'maxscale.trace.*' -delete", true);

    auto files = test.maxscale->ssh_output("ls -lh /tmp/|grep maxscale.trace").output;
    test.expect(files.empty(), "Expected no files: %s", files.c_str());

    test.maxscale->start();

    files = test.maxscale->ssh_output("ls -lh /tmp/|grep maxscale.trace").output;
    test.tprintf("%s", files.c_str());
    test.expect(!files.empty(), "Expected files: %s", files.c_str());

    create_load(test, 5);

    files = test.maxscale->ssh_output("ls -lh /tmp/|grep maxscale.trace").output;
    test.tprintf("After 5 rouns:\n%s", files.c_str());
    test.expect(mxb::strtok(files, "\n").size() < 10, "Expected less than 10 files");

    create_load(test, 45);

    files = test.maxscale->ssh_output("ls -lh /tmp/|grep maxscale.trace").output;
    test.tprintf("After 50 rouns:\n%s", files.c_str());
    test.expect(mxb::strtok(files, "\n").size() == 10, "Expected exactly 10 files");

    test.maxscale->restart();

    files = test.maxscale->ssh_output("ls -lh /tmp/|grep maxscale.trace").output;
    test.tprintf("After restarting:\n%s", files.c_str());
    test.expect(mxb::strtok(files, "\n").size() == 10, "Expected exactly 10 files");
}

int main(int argc, char** argv)
{
    return TestConnections().run_test(argc, argv, test_main);
}
