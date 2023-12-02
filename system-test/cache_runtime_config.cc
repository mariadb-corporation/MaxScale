/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <iostream>
#include <string>
#include <vector>
#include <maxbase/string.hh>
#include <maxtest/maxrest.hh>
#include <maxtest/testconnections.hh>

using namespace std;

namespace
{

const string rules_tail("/cache_runtime_config.json");
const string rules_tail1("/cache_runtime_config1.json");
const string rules_tail2("/cache_runtime_config2.json");

}


class TestCase
{
public:
    TestCase(TestConnections* pTest)
        : m_test(*pTest)
        , m_conn(m_test.maxscale->readconn_master())
    {
        m_test.expect(m_conn.connect(), "Could not connect to MaxScale.");

        init();
    }

    ~TestCase()
    {
        finish();
    }

    enum class Expect
    {
        CACHED,
        NOT_CACHED
    };

    void test_if_cached(Connection& conn, Expect expect)
    {
        auto before = conn.rows("SELECT * FROM cache_runtime_config");
        auto nBefore = before.size();

        conn.query("INSERT INTO test.cache_runtime_config VALUES (1)");

        auto after = conn.rows("SELECT * FROM cache_runtime_config");
        auto nAfter = after.size();

        if (expect == Expect::CACHED)
        {
            m_test.expect(nBefore == nAfter, "An inserted item was not cached, although expected to be.");
        }
        else
        {
            m_test.expect(nBefore != nAfter, "An inserted item was cached, although expected not to be.");
        }
    }

    void run(Connection& conn, const string& filter)
    {
        m_test.tprintf("CASE: %s", filter.c_str());

        m_test.tprintf("Testing that caching is active.");
        test_if_cached(conn, Expect::CACHED);

        string rules_file1 = m_test.maxscale->access_homedir() + rules_tail1;

        string command("sed -i \"s/cache_runtime_config/some_other_table/\" ");
        command += rules_file1;

        m_test.maxscale->ssh_node(command.c_str(), true);

        m_test.tprintf("Testing that caching is still active (rules changed, but should not have been read).");
        test_if_cached(conn, Expect::CACHED);

        MaxRest maxrest(&m_test);

        string path("filters/");
        path += filter;

        // The change of any configuration parameters will trigger a refresh of the rules.
        maxrest.alter(path, {{ "debug", 0 }});

        m_test.tprintf("Testing that caching is not active (rules should have been refreshed).");
        test_if_cached(conn, Expect::NOT_CACHED);

        string rules_file2 = m_test.maxscale->access_homedir() + rules_tail2;
        maxrest.alter(path, {{ "rules", rules_file2 }});

        m_test.tprintf("Testing that caching is active (original rules read from new rules file).");
        test_if_cached(conn, Expect::CACHED);
    }

    void run()
    {
        run(m_conn, "Cache-Shared");

        // Reset the situation to what it was.
        string rules_file1 = m_test.maxscale->access_homedir() + rules_tail1;

        string command("sed -i \"s/some_other_table/cache_runtime_config/\" ");
        command += rules_file1;

        m_test.maxscale->ssh_node(command.c_str(), true);

        Connection conn = m_test.maxscale->readconn_slave();
        m_test.expect(conn.connect(), "Could not connect to MaxScale.");

        run(conn, "Cache-Thread-Specific");
    }

private:
    void init()
    {
        finish();
        m_conn.query("CREATE TABLE cache_runtime_config (f INT)");
    }

    void finish()
    {
        m_conn.query("DROP TABLE IF EXISTS test.cache_runtime_config");
    }

    TestConnections& m_test;
    Connection       m_conn;
};


int main(int argc, char* argv[])
{
    TestConnections::skip_maxscale_start(true);
    TestConnections test(argc, argv);

    string from = mxt::SOURCE_DIR + rules_tail;
    string to1 = test.maxscale->access_homedir() + rules_tail1;
    string to2 = test.maxscale->access_homedir() + rules_tail2;

    if (test.maxscale->copy_to_node(from, to1) && test.maxscale->copy_to_node(from, to2))
    {
        string command1 = string("chmod a+r ") + to1;
        string command2 = string("chmod a+r ") + to2;

        if (test.maxscale->ssh_node(command1, true) == 0 && test.maxscale->ssh_node(command2, true) == 0)
        {
            test.maxscale->start();

            if (test.ok())
            {
                sleep(1);
                test.maxscale->wait_for_monitor();

                TestCase tc(&test);

                tc.run();
            }
            else
            {
                test.expect(false, "Could not start MaxScale.");
            }
        }
        else
        {
            test.expect(false, "Could not chmod rules files.");
        }
    }
    else
    {
        test.expect(false, "Could not copy rules files to maxscale_000.");
    }

    return test.global_result;
}
