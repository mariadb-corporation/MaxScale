/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-02-27
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxtest/testconnections.hh>

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);

    auto res = test.maxscale->ssh_output("test -d /usr/share/maxscale/gui");
    test.expect(res.rc == 0, "GUI files not found at: /usr/share/maxscale/gui/");

    res = test.maxscale->ssh_output("curl -s -f localhost:8989/index.html");

    test.expect(res.rc == 0, "Root resource should serve the GUI main page.");
    test.expect(res.output.find("<script") != std::string::npos,
                "GUI main page should load javascript: %s", res.output.c_str());

    return test.global_result;
}
