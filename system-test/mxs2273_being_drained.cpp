/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-07-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/*
 * MXS-2273: Introduce server state DRAINING
 * https://jira.mariadb.org/browse/MXS-2273
 */

#include <maxtest/testconnections.hh>
#include <iostream>
#include <string>
#include <regex>

using namespace std;

namespace
{

// NOTE: We only use 3 servers in this test 1 master + 2 slaves.

const string server1("server1");
const string server2("server2");
const string server3("server3");

enum class Expectation
{
    INCLUDES,
    EXCLUDES
};

void check_state(TestConnections& test,
                 const string& server,
                 Expectation expectation,
                 const string& what)
{
    if (expectation == Expectation::INCLUDES)
    {
        test.tprintf("%s: Expecting state to contain '%s'.", server.c_str(), what.c_str());
    }
    else
    {
        test.tprintf("%s: Expecting state to NOT contain '%s'.", server.c_str(), what.c_str());
    }

    string command = "api get servers/" + server + " data.attributes.state";

    auto result = test.maxctrl(command);

    bool found = std::regex_search(result.output, std::regex(what));

    if (expectation == Expectation::INCLUDES)
    {
        test.expect(found, "%s: State '%s' did not contain '%s'.",
                    server.c_str(), result.output.c_str(), what.c_str());
    }
    else
    {
        test.expect(!found, "%s: State '%s' unexpectedly contained '%s'.",
                    server.c_str(), result.output.c_str(), what.c_str());
    }
}

void set_drain(TestConnections& test, const string& server)
{
    test.tprintf("%s: Setting 'Draining' state.\n", server.c_str());
    string command = "set server " + server + " drain";

    test.check_maxctrl(command);
    test.maxscale->wait_for_monitor();

    check_state(test, server, Expectation::INCLUDES, "Draining|Drained");
}

void clear_drain(TestConnections& test, const string& server)
{
    test.tprintf("%s: Clearing 'Draining' state.\n", server.c_str());
    string command = "clear server " + server + " drain";

    test.check_maxctrl(command);
    test.maxscale->wait_for_monitor();

    check_state(test, server, Expectation::EXCLUDES, "Draining|Drained");
}

void check_connections(TestConnections& test, const string& server, int nExpected)
{
    test.tprintf("%s: Expecting %d connections.", server.c_str(), nExpected);
    string command = "api get servers/" + server + " data.attributes.statistics.connections";

    auto result = test.maxctrl(command);

    int nConnections = atoi(result.output.c_str());

    test.expect(nConnections == nExpected, "%s: expected %d connections, found %d.",
                server.c_str(), nExpected, nConnections);

    if (nConnections == 0)
    {
        // A server with no connections shouldn't be in Draining state
        check_state(test, server, Expectation::EXCLUDES, "Draining");
    }
}

void smoke_test(TestConnections& test, Connection& conn)
{
    // One to all...
    test.expect(conn.query("SET @a=1"), "Query failed: %s", conn.error());
    // ...and one to some slave.
    test.expect(conn.query("SELECT 1"), "Query failed: %s", conn.error());
}

void test_rws(TestConnections& test)
{
    test.tprintf("Testing draining with RWS\n");

    Connection conn1 = test.maxscale->rwsplit();
    test.expect(conn1.connect(), "Connection failed: %s", conn1.error());
    smoke_test(test, conn1);

    // Drain server3.
    set_drain(test, server3);

    // Still works?
    smoke_test(test, conn1);

    Connection conn2 = test.maxscale->rwsplit();
    test.expect(conn2.connect(), "Connection failed: %s", conn2.error());
    smoke_test(test, conn2);

    // With server3 being drained, there should now be 2,2,1 connections.
    check_connections(test, server1, 2);
    check_connections(test, server2, 2);
    check_connections(test, server3, 1);

    // Still works?
    smoke_test(test, conn1);
    smoke_test(test, conn2);

    // Undrain server3 and drain server2.
    clear_drain(test, server3);
    set_drain(test, server2);

    // This should work as the master (server1) and one slave (server3) is available.
    Connection conn3 = test.maxscale->rwsplit();
    test.expect(conn3.connect(), "Connection failed: %s", conn3.error());
    smoke_test(test, conn3);

    // A connection should have been created to the server1 (master) and server3,
    // so there should now be 3,2,2 connections.
    check_connections(test, server1, 3);
    check_connections(test, server2, 2);
    check_connections(test, server3, 2);

    // Ok, no servers being drained after this.
    clear_drain(test, server2);

    // So, this should work.
    Connection conn4 = test.maxscale->rwsplit();
    test.expect(conn4.connect(), "Connection failed: %s", conn4.error());
    smoke_test(test, conn4);

    // And all connections shold have been bumped by one.
    check_connections(test, server1, 4);
    check_connections(test, server2, 3);
    check_connections(test, server3, 3);
}

void test_rcr(TestConnections& test)
{
    test.tprintf("Testing draining with RCR\n");

    Connection conn1 = test.maxscale->readconn_master();
    test.expect(conn1.connect(), "Connection failed: %s", conn1.error());
    smoke_test(test, conn1);

    // Drain server2 and server3.
    set_drain(test, server2);
    set_drain(test, server3);

    Connection conn2 = test.maxscale->readconn_master();
    test.expect(conn2.connect(), "Connection failed: %s", conn2.error());
    smoke_test(test, conn2);

    clear_drain(test, server2);
    clear_drain(test, server3);

    smoke_test(test, conn1);
    smoke_test(test, conn2);

    check_connections(test, server1, 2);
    check_connections(test, server2, 0);
    check_connections(test, server3, 0);

    set_drain(test, server2);

    Connection conn4 = test.maxscale->readconn_slave();
    test.expect(conn4.connect(), "Connection failed: %s", conn4.error());
    smoke_test(test, conn4);

    // With server2 being drained, server3 should have been chosen.
    check_connections(test, server2, 0);
    check_connections(test, server3, 1);

    clear_drain(test, server2);
    set_drain(test, server3);

    Connection conn5 = test.maxscale->readconn_slave();
    test.expect(conn5.connect(), "Connection failed: %s", conn5.error());
    smoke_test(test, conn5);

    // With server3 being drained, server2 should have been chosen.
    check_connections(test, server2, 1);
    check_connections(test, server3, 1);

    // Now both slaves will be drained.
    set_drain(test, server2);

    Connection conn6 = test.maxscale->readconn_slave();
    test.expect(conn6.connect(), "Connection failed: %s", conn6.error());
    smoke_test(test, conn6);

    // With both slaves being drained, master should have been chosen.
    check_connections(test, server1, 3);

    clear_drain(test, server2);
    clear_drain(test, server3);
}
}

int main(int argc, char* argv[])
{
    TestConnections test(argc, argv);

    // As of 2.5.0, the master cannot be drained
    auto res = test.maxctrl("set server server1 drain");
    test.expect(res.rc != 0, "Should not be able to set master into `Draining` state");

    test_rws(test);
    test_rcr(test);

#ifdef SS_DEBUG
    // During development, check that the tests do not leave the servers
    // in 'Draining' state.
    check_state(test, server1, Expectation::EXCLUDES, "Draining|Drained");
    check_state(test, server2, Expectation::EXCLUDES, "Draining|Drained");
    check_state(test, server3, Expectation::EXCLUDES, "Draining|Drained");
#endif

    return test.global_result;
}
