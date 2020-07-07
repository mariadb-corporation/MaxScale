/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-07-07
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <iostream>
#include <iterator>
#include <string>
#include <sstream>
#include <thread>
#include <vector>
#include <random>
#include <maxtest/testconnections.h>
#include "fail_switch_rejoin_common.cpp"

using namespace std;

// How often the monitor checks the server state.
// NOTE: Ensure this is identical with the value in the configuration file.
const time_t MONITOR_INTERVAL = 1;

// After how many seconds should the failover/rejoin operation surely have
// been performed. Not very critical.
const time_t FAILOVER_DURATION = 5;

// The test now runs only two failovers. Change for a longer time limit later.
// TODO: add semisync to remove this limitation.

#define CMESSAGE(msg) \
    do { \
        stringstream ss; \
        ss << "client(" << m_id << ") : " << msg << "\n"; \
        cout << ss.str() << flush; \
    } while (false)

#if !defined (NDEBUG)

#define ss_dassert(x) do {if (!(x)) {fprintf(stderr, "Assertion failed: %s\n", #x); abort();}} while (false)
#define ss_debug(x)   x

#else

#define ss_dassert(s)
#define ss_debug(x)

#endif


namespace
{

class Client
{
public:
    enum
    {
        DEFAULT_N_CLIENTS = 4,
        DEFAULT_N_ROWS    = 100
    };

    static void init(TestConnections& test, size_t nClients, size_t nRows)
    {
        s_nClients = nClients;
        s_nRows = nRows;

        if (create_tables(test))
        {
            if (insert_data(test))
            {
                cout << "\nSyncing slaves." << endl;
                test.repl->sync_slaves();
            }
        }
    }

    static void start(bool verbose,
                      const char* zHost,
                      int port,
                      const char* zUser,
                      const char* zPassword)
    {
        for (size_t i = 0; i < s_nClients; ++i)
        {
            s_threads.push_back(std::thread(&Client::thread_main,
                                            i,
                                            verbose,
                                            zHost,
                                            port,
                                            zUser,
                                            zPassword));
        }
    }

    static void stop()
    {
        s_shutdown = true;

        for (size_t i = 0; i < s_nClients; ++i)
        {
            s_threads[i].join();
        }
    }

private:
    Client(int id, bool verbose)
        : m_id(id)
        , m_verbose(verbose)
        , m_value(1)
        , m_rand_dist(0.0, 1.0)
    {
    }

    enum action_t
    {
        ACTION_SELECT,
        ACTION_UPDATE
    };

    action_t action() const
    {
        double d = random_decimal_fraction();

        // 20% updates
        // 80% selects
        if (d <= 0.2)
        {
            return ACTION_UPDATE;
        }
        else
        {
            return ACTION_SELECT;
        }
    }

    bool run(MYSQL* pConn)
    {
        bool rv = false;

        switch (action())
        {
        case ACTION_SELECT:
            rv = run_select(pConn);
            break;

        case ACTION_UPDATE:
            rv = run_update(pConn);
            break;

        default:
            ss_dassert(!true);
        }

        return rv;
    }

    bool run_select(MYSQL* pConn)
    {
        bool rv = true;

        string stmt("SELECT * FROM test.t");
        stmt += std::to_string(m_id);
        stmt += " WHERE id=";
        stmt += std::to_string(get_random_id());

        if (mysql_query(pConn, stmt.c_str()) == 0)
        {
            flush_response(pConn);
        }
        else
        {
            if (m_verbose)
            {
                CMESSAGE("\"" << stmt << "\" failed: " << mysql_error(pConn));
            }
            rv = false;
        }

        return rv;
    }

    bool run_update(MYSQL* pConn)
    {
        bool rv = true;

        string stmt("UPDATE test.t");
        stmt += std::to_string(m_id);
        stmt += " SET id=";
        stmt += std::to_string(m_value);
        stmt += " WHERE id=";
        stmt += std::to_string(get_random_id());
        m_value = (m_value + 1) % s_nRows;

        if (mysql_query(pConn, stmt.c_str()) == 0)
        {
            flush_response(pConn);
        }
        else
        {
            if (m_verbose)
            {
                CMESSAGE("\"" << stmt << "\" failed: " << mysql_error(pConn));
            }
            rv = false;
        }

        return rv;
    }

    static void flush_response(MYSQL* pConn)
    {
        do
        {
            MYSQL_RES* pRes = mysql_store_result(pConn);
            mysql_free_result(pRes);
        }
        while (mysql_next_result(pConn) == 0);
    }

    int get_random_id() const
    {
        int id = s_nRows * random_decimal_fraction();

        ss_dassert(id >= 0);
        ss_dassert(id <= (int)s_nRows);

        return id;
    }

    double random_decimal_fraction() const
    {
        return m_rand_dist(m_rand_gen);
    }

    void run(const char* zHost, int port, const char* zUser, const char* zPassword)
    {
        do
        {
            MYSQL* pMysql = mysql_init(NULL);

            if (pMysql)
            {
                unsigned int timeout = 5;
                mysql_options(pMysql, MYSQL_OPT_CONNECT_TIMEOUT, &timeout);
                mysql_options(pMysql, MYSQL_OPT_READ_TIMEOUT, &timeout);
                mysql_options(pMysql, MYSQL_OPT_WRITE_TIMEOUT, &timeout);

                if (m_verbose)
                {
                    CMESSAGE("Connecting");
                }

                if (mysql_real_connect(pMysql, zHost, zUser, zPassword, "test", port, NULL, 0))
                {
                    if (m_verbose)
                    {
                        CMESSAGE("Connected.");
                    }

                    while (!s_shutdown && run(pMysql))
                    {
                    }
                }
                else
                {
                    if (m_verbose)
                    {
                        CMESSAGE("mysql_real_connect() failed: " << mysql_error(pMysql));
                    }
                }

                if (m_verbose)
                {
                    CMESSAGE("Closing");
                }
                mysql_close(pMysql);
            }
            else
            {
                CMESSAGE("mysql_init() failed.");
            }

            // To prevent some backend from becoming overwhelmed.
            sleep(1);
        }
        while (!s_shutdown);
    }

    static void thread_main(int i,
                            bool verbose,
                            const char* zHost,
                            int port,
                            const char* zUser,
                            const char* zPassword)
    {
        if (mysql_thread_init() == 0)
        {
            Client client(i, verbose);

            client.run(zHost, port, zUser, zPassword);

            mysql_thread_end();
        }
        else
        {
            int m_id = i;
            CMESSAGE("mysql_thread_init() failed.");
        }
    }

    static bool create_tables(TestConnections& test)
    {
        cout << "\nCreating tables." << endl;

        MYSQL* pConn = test.maxscales->conn_rwsplit[0];

        string drop_head("DROP TABLE IF EXISTS test.t");
        string create_head("CREATE TABLE test.t");
        string create_tail(" (id INT)");

        for (size_t i = 0; i < s_nClients; ++i)
        {
            string drop = drop_head + std::to_string(i);
            test.try_query(pConn, "%s", drop.c_str());

            string create = create_head + std::to_string(i) + create_tail;
            test.try_query(pConn, "%s", create.c_str());
        }

        return test.ok();
    }

    static bool insert_data(TestConnections& test)
    {
        cout << "\nInserting data." << endl;

        MYSQL* pConn = test.maxscales->conn_rwsplit[0];

        for (size_t i = 0; i < s_nClients; ++i)
        {
            string insert("insert into test.t");
            insert += std::to_string(i);
            insert += " values ";

            for (size_t j = 0; j < s_nRows; ++j)
            {
                insert += "(";
                insert += std::to_string(j);
                insert += ")";

                if (j < s_nRows - 1)
                {
                    insert += ", ";
                }
            }

            test.try_query(pConn, "%s", insert.c_str());
        }

        return test.ok();
    }

private:
    enum
    {
        INITSTATE_SIZE = 32
    };

    size_t                                         m_id;
    bool                                           m_verbose;
    size_t                                         m_value;
    mutable std::mt19937                           m_rand_gen;
    mutable std::uniform_real_distribution<double> m_rand_dist;

    static size_t s_nClients;
    static size_t s_nRows;
    static bool   s_shutdown;

    static std::vector<std::thread> s_threads;
};

size_t Client::s_nClients;
size_t Client::s_nRows;
bool Client::s_shutdown;
std::vector<std::thread> Client::s_threads;
}

namespace
{

void list_servers(TestConnections& test)
{
    test.print_maxctrl("list servers");
}

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

bool check_server_status(TestConnections& test, int id)
{
    bool is_master = false;

    Mariadb_nodes* pRepl = test.repl;

    string server = string("server") + std::to_string(id);

    StringSet statuses = test.get_server_status(server.c_str());
    std::ostream_iterator<string> oi(cout, " ");

    cout << server << ": ";
    std::copy(statuses.begin(), statuses.end(), oi);

    cout << " => ";

    if (statuses.count("Master"))
    {
        is_master = true;
        cout << "OK";
    }
    else if (statuses.count("Slave"))
    {
        cout << "OK";
    }
    else if (statuses.count("Running"))
    {
        MYSQL* pConn = pRepl->nodes[id - 1];

        char result[1024];
        if (find_field(pConn, "SHOW SLAVE STATUS", "Last_IO_Error", result) == 0)
        {
            const char needle[] =
                ", which is not in the master's binlog. "
                "Since the master's binlog contains GTIDs with higher sequence numbers, "
                "it probably means that the slave has diverged due to executing extra "
                "erroneous transactions";

            if (strstr(result, needle))
            {
                // A rejoin was attempted, but it failed because the node (old master)
                // had events that were not present in the new master. That is, a rejoin
                // is not possible in principle without corrective action.
                cout << "OK (could not be joined due to GTID issue)";
            }
            else
            {
                cout << result;
                test.expect(false, "Merely 'Running' node did not error in expected way.");
            }
        }
        else
        {
            test.expect(false, "Could not execute \"SHOW SLAVE STATUS\"");
        }
    }
    else
    {
        test.expect(false, "Unexpected server state for %s.", server.c_str());
    }

    cout << endl;

    return is_master;
}

void check_server_statuses(TestConnections& test)
{
    int masters = 0;

    masters += check_server_status(test, 1);
    masters += check_server_status(test, 2);
    masters += check_server_status(test, 3);
    masters += check_server_status(test, 4);

    if (masters == 0)
    {
        test.global_result = 0;
        test.tprintf("No master, checking that autofail has been turned off.");
        test.log_includes(0, "disabling automatic failover");
    }
    else if (masters != 1)
    {
        test.expect(!true, "Unexpected number of masters: %d", masters);
    }
}

bool is_valid_server_id(TestConnections& test, int id)
{
    std::set<int> ids;
    test.repl->connect();

    for (int i = 0; i < test.repl->N; i++)
    {
        ids.insert(test.repl->get_server_id(i));
    }

    test.repl->disconnect();
    return ids.count(id);
}

void run(TestConnections& test)
{
    cout << "\nConnecting to MaxScale." << endl;
    test.maxscales->connect_maxscale();

    Client::init(test, Client::DEFAULT_N_CLIENTS, Client::DEFAULT_N_ROWS);

    if (test.ok())
    {
        const char* zHost = test.maxscales->IP[0];
        int port = test.maxscales->rwsplit_port[0];
        const char* zUser = test.maxscales->user_name;
        const char* zPassword = test.maxscales->password;

        cout << "Connecting to " << zHost << ":" << port << " as " << zUser << ":" << zPassword << endl;
        cout << "Starting clients." << endl;
        Client::start(test.verbose, zHost, port, zUser, zPassword);

        list_servers(test);

        for (int i = 0; i < 2; i++)
        {
            test.set_timeout(20);
            test.maxscales->wait_for_monitor();

            int master_id = get_master_server_id(test);

            if (is_valid_server_id(test, master_id))
            {
                test.set_timeout(20);
                cout << "\nStopping node: " << master_id << endl;
                test.repl->stop_node(master_id - 1);

                test.maxscales->wait_for_monitor();
                list_servers(test);

                test.maxscales->wait_for_monitor();
                list_servers(test);

                test.set_timeout(20);
                test.maxscales->wait_for_monitor();
                cout << "\nStarting node: " << master_id << endl;
                test.repl->start_node(master_id - 1);

                test.maxscales->wait_for_monitor();
                list_servers(test);

                test.maxscales->wait_for_monitor();
                list_servers(test);
            }
            else
            {
                test.expect(false, "Unexpected master id: %d", master_id);
            }
        }

        test.maxscales->wait_for_monitor();

        cout << "\nStopping clients.\n" << flush;
        Client::stop();

        test.repl->close_connections();
        test.repl->connect();

        check_server_statuses(test);
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
