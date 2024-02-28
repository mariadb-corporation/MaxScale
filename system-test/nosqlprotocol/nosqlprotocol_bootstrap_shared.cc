/*
 * Copyright (c) 2020 MariaDB Corporation Ab
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

#include <maxbase/assert.hh>
#include <maxtest/maxrest.hh>
#include <maxtest/maxscales.hh>
#include <maxtest/testconnections.hh>
#include <iostream>
#include <mongocxx/client.hpp>
#include <mongocxx/instance.hpp>

using namespace std;

namespace
{

void required_query(TestConnections& test, MYSQL* pMysql, const char* zSql)
{
    if (::execute_query(pMysql, "%s", zSql) != 0)
    {
        string message { "Execution of '" };
        message += zSql;
        message += "': ";
        message += mysql_error(pMysql);

        ++test.global_result;
        throw std::runtime_error(message);
    }
}

void setup(TestConnections& test, MYSQL* pMysql)
{
    required_query(test, pMysql, "DROP DATABASE IF EXISTS nosqlprotocol");
    required_query(test, pMysql, "CREATE DATABASE nosqlprotocol");
    required_query(test, pMysql, "DROP USER IF EXISTS 'admin.nosql_admin'@'%'");
    required_query(test, pMysql, "CREATE USER 'admin.nosql_admin'@'%' IDENTIFIED BY 'nosql_password'");
    required_query(test, pMysql, "GRANT ALL PRIVILEGES ON *.* TO 'admin.nosql_admin'@'%' WITH GRANT OPTION");
}

void connect_to_nosql(TestConnections& test, mxt::MaxScale& maxscale,
                      const string& user, const string& password,
                      bool should_succeed)
{
    test.tprintf("Connecting as %s:%s, expected %s succeed",
                 user.c_str(), password.c_str(), should_succeed ? "TO" : "NOT to");

    bool connected = false;

    string s { "mongodb://" };
    s += user;
    s += ":";
    s += password;
    s += "@";
    s += maxscale.ip();
    s += ":4008";
    s += "/admin";

    try
    {
        mongocxx::client client { mongocxx::uri{ s } };

        // We need to perform some activity to force a connection to be made.
        vector<string> dbs = client.list_database_names();

        test.tprintf("Connected with %s:%s.", user.c_str(), password.c_str());
        connected = true;
    }
    catch (const std::exception& x)
    {
        if (should_succeed)
        {
            test.expect(!true, "Exception: %s", x.what());
        }
    }

    if (connected && !should_succeed)
    {
        test.expect(!true, "Connecting succeeded with %s:%s, although not expected to.",
                    user.c_str(), password.c_str());
    }
    else if (!connected && should_succeed)
    {
        test.expect(!true, "Connecting did not succeed with %s:%s, although expected to.",
                    user.c_str(), password.c_str());
    }
}

void test_connecting_to_nosql(TestConnections& test, mxt::MaxScale& maxscale)
{
    connect_to_nosql(test, maxscale, "nosql_admin", "nosql_password", true);
    connect_to_nosql(test, maxscale, "nosql_admin", "wrong_password", false);
    connect_to_nosql(test, maxscale, "wrong_user", "wrong_password", false);
}

bool find_master(TestConnections& test, time_t max_wait)
{
    bool found_master = false;

    auto* pMaxscale1 = test.maxscale;

    MaxRest maxrest(&test, pMaxscale1);

    time_t start = time(nullptr);
    time_t elapsed = 0;

    do
    {
        vector<MaxRest::Server> servers = maxrest.list_servers();

        for (const auto& server : servers)
        {
            if (server.state.find("Master") != string::npos)
            {
                found_master = true;
            }
        }

        elapsed = time(nullptr) - start;
    }
    while (!found_master && elapsed < max_wait);

    return found_master;
}

bool find_nosql_user(TestConnections& test, time_t max_wait)
{
    bool found_nosql_user = false;

    auto* pMaxscale1 = test.maxscale;
    auto* pMaxscale2 = test.maxscale2;

    time_t start = time(nullptr);
    time_t elapsed = 0;

    do
    {
        if (pMaxscale1->log_matches("Created initial NoSQL user") ||
            pMaxscale2->log_matches("Created initial NoSQL user"))
        {
            found_nosql_user = true;
        }

        elapsed = time(nullptr) - start;
    }
    while (!found_nosql_user && elapsed < max_wait);

    return found_nosql_user;
}

void test_main(TestConnections& test)
{
    mxt::ReplicationCluster* pMS = test.repl;
    mxb_assert(pMS);

    if (pMS->connect() == 0)
    {
        MYSQL* pMaster = pMS->nodes[0];

        try
        {
            setup(test, pMaster);
        }
        catch (const std::exception& x)
        {
            test.tprintf("Error: %s", x.what());
        }

        if (test.global_result == 0)
        {
            auto* pMaxscale1 = test.maxscale;
            auto* pMaxscale2 = test.maxscale2;

            pMaxscale1->start();
            pMaxscale2->start();

            time_t max_wait = 10; // seconds

            if (find_master(test, max_wait))
            {
                if (find_nosql_user(test, max_wait))
                {
                    mongocxx::instance inst{};

                    test_connecting_to_nosql(test, *pMaxscale1);
                    test_connecting_to_nosql(test, *pMaxscale2);
                }
                else
                {
                    test.expect(!true, "Did not find initial NoSQL user within %d seconds.", (int)max_wait);
                }
            }
            else
            {
                test.expect(!true, "Did not find master within %d seconds.", (int)max_wait);
            }
        }
    }
    else
    {
        ++test.global_result;
        test.tprintf("Could not connect to master.");
    }
}

}

int main(int argc, char* argv[])
{
    TestConnections::skip_maxscale_start(true);

    TestConnections test;

    return test.run_test(argc, argv, test_main);
}

