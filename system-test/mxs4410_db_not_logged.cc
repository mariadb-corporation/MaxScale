/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-04-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxtest/testconnections.hh>
#include <fstream>
#include <iostream>

using namespace std;

namespace
{

vector<string> get_lines(TestConnections& test, const string& log)
{
    // This should make sure that the file is flushed to disk by GCUpdated if the unified file is used.
    this_thread::sleep_for(500ms);

    const char* zTmp_file = "./mxs4410.txt";

    vector<string> rv;
    test.maxscale->copy_from_node(log.c_str(), zTmp_file);
    ifstream infile(zTmp_file);

    for (string line; getline(infile, line);)
    {
        rv.push_back(line);
    }

    remove(zTmp_file);

    return rv;
}

void query(TestConnections& test, Connection& c, const char* zStmt)
{
    test.expect(c.query(zStmt), "\"%s\" failed: %s", zStmt, c.error());
}

}


int main(int argc, char** argv)
{
    TestConnections test(argc, argv);

    // Clean up old files
    test.maxscale->ssh_node("rm -f /tmp/qla_mxs4410.log.*", true);

    auto c = test.maxscale->rwsplit();
    test.expect(c.connect(), "Could not connect: %s", c.error());

    query(test, c, "CREATE DATABASE IF NOT EXISTS mxs4410");
    query(test, c, "USE mxs4410");
    query(test, c, "DROP DATABASE mxs4410");

    auto lines = get_lines(test, "/tmp/qla_mxs4410.log.1");

    for (auto line: lines)
    {
        cout << line << endl;
    }

    test.expect(lines.size() > 1 && lines.back() == "mxs4410", "QLA log did not contain mxs4410.");

    return test.global_result;
}
