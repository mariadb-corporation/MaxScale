/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-03-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <iostream>
#include <maxbase/string.hh>
#include <maxbase/jansson.hh>
#include <maxtest/maxrest.hh>
#include <maxtest/testconnections.hh>
#include <maxbase/format.hh>

namespace
{

const std::string monitor_name = "Xpand-Monitor";

void test_login(TestConnections& test, int port, const char* user, const char* pw, const char* db,
                bool expect_success)
{
    auto ip = test.maxscale->ip();

    MYSQL* rwsplit_conn = db ? open_conn_db(port, ip, db, user, pw) :
        open_conn_no_db(port, ip, user, pw);

    if (expect_success)
    {
        test.expect(mysql_errno(rwsplit_conn) == 0, "RWSplit connection failed: '%s'",
                    mysql_error(rwsplit_conn));
        if (test.ok())
        {
            test.try_query(rwsplit_conn, "select rand();");
            test.tprintf("%s logged in and queried", user);
        }
    }
    else
    {
        test.expect(mysql_errno(rwsplit_conn) != 0,
                    "RWSplit connection succeeded when failure was expected");
    }
    mysql_close(rwsplit_conn);
}

void test_main(TestConnections& test)
{
    // TODO: Complete the test. Will currently fail due to XpandMon changes.
    const char svc_user[] = "rwsplit_user";
    const char svc_user_host[] = "'rwsplit_user'@'%'";
    const char svc_pw[] = "rwsplit_pw";
    const char db_user[] = "tester1";
    const char db_user_host[] = "'tester1'@'%'";
    const char db_pw[] = "tester1_pw";

    // MXS-3934: Services created at runtime don't work with xpandmon
    test.check_maxctrl("create service my-test-service readwritesplit user=maxskysql password=skysql");
    test.check_maxctrl("link service my-test-service Xpand-Monitor");
    test.check_maxctrl("create listener my-test-service my-test-listener 4009");

    int port = 4009;
    test_login(test, port, svc_user, svc_pw, nullptr, true);
    test_login(test, port, db_user, db_pw, "test", true);

    // MXS-3938: Should be possible to unlink servers
    if (test.ok())
    {
        test.check_maxctrl("unlink monitor Xpand-Monitor xpand_server2 xpand_server3 xpand_server4");

        // Remove the created (if success) dynamic config file, so as not to cause trouble later.
        test.maxscale->ssh_node("rm -f /var/lib/maxscale/maxscale.cnf.d/Xpand-Monitor.cnf", true);
    }
}
}

int main(int argc, char* argv[])
{
    return TestConnections().run_test(argc, argv, test_main);
}
