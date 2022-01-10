/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-01-04
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <iostream>
#include <string>
#include <maxtest/testconnections.hh>

using std::cerr;
using std::cout;
using std::endl;
using std::flush;
using std::string;
using std::stringstream;

namespace
{

void create_table(TestConnections& test)
{
    MYSQL* pConn = test.maxscale->conn_rwsplit;

    test.try_query(pConn, "DROP TABLE IF EXISTS test.t1");
    test.try_query(pConn, "CREATE TABLE test.t1(id INT)");
}

int i_start = 0;
int n_rows = 20;
int i_end = 0;

void insert_data(TestConnections& test)
{
    MYSQL* pConn = test.maxscale->conn_rwsplit;

    test.try_query(pConn, "BEGIN");

    i_end = i_start + n_rows;

    for (int i = i_start; i < i_end; ++i)
    {
        stringstream ss;
        ss << "INSERT INTO test.t1 VALUES (" << i << ")";
        test.try_query(pConn, "%s", ss.str().c_str());
    }

    test.try_query(pConn, "COMMIT");

    i_start = i_end;
}

void run(TestConnections& test)
{
    auto& mxs = *test.maxscale;
    mxs.wait_for_monitor();

    const int N = 4;

    auto master = mxt::ServerInfo::MASTER | mxt::ServerInfo::RUNNING;
    auto slave = mxt::ServerInfo::SLAVE | mxt::ServerInfo::RUNNING;
    auto normal_status = {master, slave, slave, slave};
    mxs.check_servers_status(normal_status);

    cout << "\nConnecting to MaxScale." << endl;
    test.maxscale->connect_maxscale();

    cout << "\nCreating table." << endl;
    create_table(test);

    cout << "\nInserting data." << endl;
    insert_data(test);

    cout << "\nSyncing slaves." << endl;
    test.repl->sync_slaves();

    cout << "\nTrying to do manual switchover to server2" << endl;
    test.maxctrl("call command mysqlmon switchover MySQL-Monitor server2 server1");

    mxs.wait_for_monitor();
    mxs.check_servers_status({slave, master, slave, slave});

    if (test.ok())
    {
        cout << "\nSwitchover success. Resetting situation using async-switchover\n";
        test.maxctrl("call command mariadbmon async-switchover MySQL-Monitor server1");
        // Wait a bit so switch completes, then fetch results.
        mxs.wait_for_monitor(2);
        auto res = test.maxctrl("call command mariadbmon fetch-cmd-results MySQL-Monitor");
        const char cmdname[] = "fetch-cmd-results";
        test.expect(res.rc == 0, "%s failed: %s", cmdname, res.output.c_str());
        if (test.ok())
        {
            // The output is a json string. Check that it includes "switchover completed successfully".
            auto found = (res.output.find("switchover completed successfully") != string::npos);
            test.expect(found, "Result json did not contain expected message. Result: %s",
                        res.output.c_str());
        }
        mxs.check_servers_status(normal_status);
    }
}
}

int main(int argc, char* argv[])
{
    TestConnections test(argc, argv);

    run(test);

    return test.global_result;
}
