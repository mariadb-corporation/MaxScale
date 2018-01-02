/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2020-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <iostream>
#include <iterator>
#include <string>
#include <sstream>
#include "testconnections.h"

using std::cerr;
using std::cout;
using std::endl;
using std::flush;
using std::string;
using std::stringstream;

namespace
{

void sleep(int s)
{
    cout << "Sleeping " << s << " times 1 second" << flush;
    do
    {
        ::sleep(1);
        cout << "." << flush;
        --s;
    }
    while (s > 0);

    cout << endl;
}

}

namespace
{

void create_table(TestConnections& test)
{
    MYSQL* pConn = test.maxscales->conn_rwsplit[0];

    test.try_query(pConn, "DROP TABLE IF EXISTS test.t1");
    test.try_query(pConn, "CREATE TABLE test.t1(id INT)");
}

static int i_start = 0;
static int n_rows = 20;
static int i_end = 0;

void insert_data(TestConnections& test)
{
    MYSQL* pConn = test.maxscales->conn_rwsplit[0];

    test.try_query(pConn, "BEGIN");

    i_end = i_start + n_rows;

    for (int i = i_start; i < i_end; ++i)
    {
        stringstream ss;
        ss << "INSERT INTO test.t1 VALUES (" << i << ")";
        test.try_query(pConn, ss.str().c_str());
    }

    test.try_query(pConn, "COMMIT");

    i_start = i_end;
}

void expect(TestConnections& test, const char* zServer, const StringSet& expected)
{
    StringSet found = test.get_server_status(zServer);

    std::ostream_iterator<string> oi(cout, ", ");

    cout << zServer
         << ", expected states: ";
    std::copy(expected.begin(), expected.end(), oi);
    cout << endl;

    cout << zServer
         << ", found states   : ";
    std::copy(found.begin(), found.end(), oi);
    cout << endl;

    if (found != expected)
    {
        cout << "ERROR, found states are not the same as the expected ones." << endl;
        ++test.global_result;
    }

    cout << endl;
}

void expect(TestConnections& test, const char* zServer, const char* zState)
{
    StringSet s;
    s.insert(zState);

    expect(test, zServer, s);
}

void expect(TestConnections& test, const char* zServer, const char* zState1, const char* zState2)
{
    StringSet s;
    s.insert(zState1);
    s.insert(zState2);

    expect(test, zServer, s);
}

void run(TestConnections& test)
{
    sleep(5);

    int N = test.repl->N;
    cout << "Nodes: " << N << endl;

    expect(test, "server1", "Master", "Running");
    expect(test, "server2", "Slave", "Running");
    expect(test, "server3", "Slave", "Running");
    expect(test, "server4", "Slave", "Running");

    cout << "\nConnecting to MaxScale." << endl;
    test.maxscales->connect_maxscale(0);

    cout << "\nCreating table." << endl;
    create_table(test);

    cout << "\nInserting data." << endl;
    insert_data(test);

    cout << "\nSyncing slaves." << endl;
    test.repl->sync_slaves();

    cout << "\nStopping slave " << N - 1 << endl;
    test.repl->stop_node(N - 1);

    sleep(8);

    // server4 was stopped, so we expect the state of it to be /Down/,
    // and the states of the other ones not to have changed.
    expect(test, "server1", "Master", "Running");
    expect(test, "server2", "Slave", "Running");
    expect(test, "server3", "Slave", "Running");
    expect(test, "server4", "Down");

    cout << "\nClosing connection to MaxScale." << endl;
    test.maxscales->close_maxscale_connections(0);

    cout << "\nConnecting to MaxScale." << endl;
    test.maxscales->connect_maxscale(0);

    cout << "\nInserting data." << endl;
    insert_data(test);

    cout << "\nSyncing slaves." << endl;
    test.repl->sync_slaves();

    cout << "\nStopping master." << endl;
    test.repl->stop_node(0);

    sleep(8);

    // server1 (previous master) was taken down, so its state should be /Down/.
    // server2 should have been made into master, and server4 should still be down.
    expect(test, "server1", "Down");
    expect(test, "server2", "Master", "Running");
    expect(test, "server3", "Slave", "Running");
    expect(test, "server4", "Down");

    cout << "\nBringing up slave " << N - 1 << endl;
    test.repl->start_node(N - 1, (char*)"");

    sleep(8);

    // server1 should still be down, server2 still master, and server3 still
    // a slave. server4 was brought up, but as auto_rejoin is false, it should
    // be Running, but not Slave.
    // turned into a slave.
    expect(test, "server1", "Down");
    expect(test, "server2", "Master", "Running");
    expect(test, "server3", "Slave", "Running");
    expect(test, "server4", "Running");

    cout << "\nTrying to do manual switchover to server4" << endl;
    const char* zCommand = "call command mysqlmon switchover MySQL-Monitor server4 server2";
    test.maxscales->execute_maxadmin_command_print(0, (char*)zCommand);

    sleep(8);

    // The state should not change, as server4 is not good enough as master.
    expect(test, "server1", "Down");
    expect(test, "server2", "Master", "Running");
    expect(test, "server3", "Slave", "Running");
    expect(test, "server4", "Running");

}

}

int main(int argc, char* argv[])
{
    Mariadb_nodes::require_gtid(true);
    TestConnections test(argc, argv);

    run(test);

    return test.global_result;
}

