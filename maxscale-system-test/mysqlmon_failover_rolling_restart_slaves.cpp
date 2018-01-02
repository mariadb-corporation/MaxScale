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

void expect(TestConnections& test, const string& server, const char* zState)
{
    expect(test, server.c_str(), zState);
}

void expect(TestConnections& test, const char* zServer, const char* zState1, const char* zState2)
{
    StringSet s;
    s.insert(zState1);
    s.insert(zState2);

    expect(test, zServer, s);
}

void expect(TestConnections& test, const string& server, const char* zState1, const char* zState2)
{
    expect(test, server.c_str(), zState1, zState2);
}

string server_name(int i)
{
    stringstream ss;
    ss << "server" << (i + 1);
    return ss.str();
}

void check_server_status(TestConnections& test, int N, int down = -1)
{
    expect(test, "server1", "Master", "Running");

    for (int i = 1; i < N; ++i)
    {
        string slave = server_name(i);
        if (i == down)
        {
            expect(test, slave, "Down");
        }
        else
        {
            expect(test, slave, "Slave", "Running");
        }
    }
}

void run(TestConnections& test)
{
    sleep(5);

    int N = test.repl->N;
    cout << "Nodes: " << N << endl;

    check_server_status(test, N);

    cout << "\nConnecting to MaxScale." << endl;
    test.maxscales->connect_maxscale(0);

    cout << "\nCreating table." << endl;
    create_table(test);

    cout << "\nInserting data." << endl;
    insert_data(test);

    cout << "\nSyncing slaves." << endl;
    test.repl->sync_slaves();

    for (int i = 1; i < N; ++i)
    {
        string slave = server_name(i);

        cout << "\nStopping slave " << slave << endl;
        test.repl->stop_node(i);

        sleep(5);

        check_server_status(test, N, i);

        cout << "\nStarting slave " << slave << endl;
        test.repl->start_node(i, (char*)"");

        sleep(5);

        check_server_status(test, N);
    }
}

}

int main(int argc, char* argv[])
{
    Mariadb_nodes::require_gtid(true);
    TestConnections test(argc, argv);

    run(test);

    return test.global_result;
}

