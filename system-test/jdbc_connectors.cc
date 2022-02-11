/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-02-11
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxbase/format.hh>
#include <maxtest/testconnections.hh>
#include <maxtest/execute_cmd.hh>

using jdbc::ConnectorVersion;
using std::string;

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);
    auto host = test.maxscale->ip();
    auto port = test.maxscale->port();
    auto& user = test.maxscale->user_name();
    auto& pw = test.maxscale->password();
    string wrong_pw = "wrong_pw";

    auto testfunc = [&](ConnectorVersion vrs) {
            string select = "select selec.select from (select 'select') as selec;";
            auto res_ok = jdbc::test_connection(vrs, host, port, user, pw, select);
            auto res_fail = jdbc::test_connection(vrs, host, port, user, wrong_pw, select);

            string connector = mxb::string_printf("JDBC connector '%s'", jdbc::to_string(vrs).c_str());
            auto connectorc = connector.c_str();
            if (res_ok.success && res_ok.output == "select\n")
            {
                if (!res_fail.success)
                {
                    test.tprintf("%s test succeeded", connectorc);
                }
                else
                {
                    test.add_failure("%s succeeded with wrong password.", connectorc);
                }
            }
            else
            {
                test.add_failure("%s test failed. Rval: %i Output: '%s'",
                                 connectorc, res_ok.success, res_ok.output.c_str());
            }
        };

    testfunc(ConnectorVersion::MARIADB_250);
    testfunc(ConnectorVersion::MARIADB_270);
    testfunc(ConnectorVersion::MYSQL606);
    return test.global_result;
}
