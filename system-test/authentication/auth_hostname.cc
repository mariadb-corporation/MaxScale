/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-10-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxtest/testconnections.hh>

namespace
{
void test_main(TestConnections& test)
{
    auto& mxs = *test.maxscale;
    const char user[] = "testuser";
    const char pw1[] = "pass1";
    const char pw2[] = "pass2";
    const char pw3[] = "pass3";
    test.tprintf("Creating user '%s' with 3 different passwords for different hosts.", user);
    auto conn = mxs.open_rwsplit_connection2_nodb();
    auto user1 = conn->create_user(user, "non_existing_host1", pw1);
    auto user2 = conn->create_user(user, "%", pw2);
    auto user3 = conn->create_user(user, "non_existing_host2", pw3);

    test.repl->sync_slaves();

    const char unexpected_success[] = "Login with '%s' succeeded when it should have failed";
    test.tprintf("Trying first hostname, expecting failure");
    auto client_conn = mxs.try_open_rwsplit_connection(user, pw1);
    test.expect(!client_conn->is_open(), unexpected_success, pw1);

    test.tprintf("Trying second hostname, expecting success");
    client_conn = mxs.try_open_rwsplit_connection(user, pw2);
    test.expect(client_conn->is_open(), "Login with '%s' failed.", pw2);

    test.tprintf("Trying third hostname, expecting failure");
    client_conn = mxs.try_open_rwsplit_connection(user, pw3);
    test.expect(!client_conn->is_open(), unexpected_success, pw3);
}
}

int main(int argc, char* argv[])
{
    return TestConnections().run_test(argc, argv, test_main);
}
