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
#include <maxtest/testconnections.hh>
#include <maxbase/http.hh>

using namespace std::string_literals;

const std::string admin_file{"/var/log/maxscale/admin_audit.csv"};

int count_audit_file_lines(TestConnections& test)
{
    int ret = -1;

    auto out = test.maxscale->ssh_output("wc -l " + admin_file);

    test.expect(out.rc == 0, "Couldn't not access %s", admin_file.c_str());

    return out.rc ? -1 : atoi(out.output.c_str());
}

int main(int argc, char** argv)
{
    TestConnections::skip_maxscale_start(true);
    TestConnections test(argc, argv);

    test.maxscale->ssh_output("rm " + admin_file);
    test.maxscale->start_and_check_started();

    test.maxctrl("list servers");
    auto lines = count_audit_file_lines(test);                      // header + 1
    test.expect(lines == 2, "Expected 2 lines, got %d", lines);

    test.maxctrl("alter maxscale admin_audit_exclude_methods=GET"); // +1
    test.maxctrl("list servers");                                   // exclude
    lines = count_audit_file_lines(test);
    test.expect(lines == 3, "Expected 3 lines, got %d", lines);

    test.maxctrl("alter maxscale admin_audit_exclude_methods=");    // +1
    test.maxctrl("list servers");                                   // +1
    lines = count_audit_file_lines(test);
    test.expect(lines == 5, "Expected 5 lines, got %d", lines);

    return test.global_result;
}
