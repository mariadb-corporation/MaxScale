/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-09-06
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <random>
#include <maxtest/testconnections.hh>
#include <maxbase/format.hh>

using namespace std;

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

// How long should we keep in running.
const time_t TEST_DURATION = 60;

const char* CLIENT_USER = "mysqlmon_switchover_stress";
const char* CLIENT_PASSWORD = "mysqlmon_switchover_stress";

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

    static bool drop_tables(TestConnections& test)
    {
        test.tprintf("Dropping tables.");
        auto sConn = test.maxscale->open_rwsplit_connection2();

        for (size_t i = 0; i < s_nClients; ++i)
        {
            sConn->cmd_f("drop table test.t%zu;", i);
        }

        return test.ok();
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
        test.tprintf("Creating tables.");
        auto sConn = test.maxscale->open_rwsplit_connection2();

        for (size_t i = 0; i < s_nClients; ++i)
        {
            sConn->cmd_f("create or replace table test.t%zu (id int);", i);
        }

        return test.ok();
    }

    static bool insert_data(TestConnections& test)
    {
        test.tprintf("Inserting data.");

        auto pConn = test.maxscale->open_rwsplit_connection2();

        for (size_t i = 0; i < s_nClients; ++i)
        {
            string insert = mxb::string_printf("insert into test.t%zu values ", i);

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

            pConn->cmd(insert);
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

void create_client_user(TestConnections& test)
{
    auto conn = test.maxscale->open_rwsplit_connection2();
    conn->cmd_f("create or replace user '%s' identified by '%s';", CLIENT_USER, CLIENT_PASSWORD);
    conn->cmd_f("grant select, insert, update on test.* to '%s';", CLIENT_USER);
}

void drop_client_user(TestConnections& test)
{
    auto conn = test.maxscale->open_rwsplit_connection2();
    conn->cmd_f("drop user '%s';", CLIENT_USER);
}

void switchover(TestConnections& test, int next_master_id, int current_master_id)
{
    auto& mxs = *test.maxscale;
    string next_master_name = "server" + std::to_string(next_master_id);
    string command = mxb::string_printf("call command mysqlmon switchover MySQL-Monitor %s server%i",
                                        next_master_name.c_str(), current_master_id);
    test.tprintf("Running on MaxCtrl: %s", command.c_str());
    auto res = mxs.maxctrl(command);
    if (res.rc == 0)
    {
        mxs.wait_for_monitor();

        // Check that server statuses are as expected.
        string master_name;
        int n_master = 0;

        auto servers = mxs.get_servers();
        servers.print();

        for (int i = 0; i < 4; i++)
        {
            const auto& srv = servers.get(i);
            auto status = srv.status;
            if (status == mxt::ServerInfo::master_st)
            {
                n_master++;
                test.expect(srv.name == next_master_name, "Wrong master. Got %s, expected %s.",
                            srv.name.c_str(), next_master_name.c_str());
            }
            else if (status != mxt::ServerInfo::slave_st)
            {
                test.add_failure("%s is neither master or slave. Status: %s", srv.name.c_str(),
                                 srv.status_to_string().c_str());
            }
        }

        test.expect(n_master == 1, "Expected one master, found %i.", n_master);
    }
    else
    {
        test.add_failure("Manual switchover failed: %s", res.output.c_str());
    }
}

void run(TestConnections& test)
{
    auto& mxs = *test.maxscale;
    create_client_user(test);
    mxs.check_print_servers_status(mxt::ServersInfo::default_repl_states());

    Client::init(test, Client::DEFAULT_N_CLIENTS, Client::DEFAULT_N_ROWS);

    if (test.ok())
    {
        const char* zHost = test.maxscale->ip4();
        int port = test.maxscale->rwsplit_port;
        const char* zUser = CLIENT_USER;
        const char* zPassword = CLIENT_PASSWORD;

        test.tprintf("Starting clients. Connecting to %s:%i as '%s':'%s'.",
                     zHost, port, zUser, zPassword);
        Client::start(test.verbose(), zHost, port, zUser, zPassword);

        time_t start = time(NULL);
        int current_master_id = 1;
        int n_switchovers = 0;

        while (test.ok() && (time(NULL) - start < TEST_DURATION))
        {
            int next_master_id = current_master_id % 4 + 1;
            switchover(test, next_master_id, current_master_id);

            if (test.ok())
            {
                current_master_id = next_master_id;
                n_switchovers++;
                sleep(1);
            }
        }

        test.tprintf("Stopping clients after %i switchovers.", n_switchovers);
        Client::stop();

        // Ensure master is at server1. Shortens startup time for next test.
        if (current_master_id != 1)
        {
            switchover(test, 1, current_master_id);
        }

        mxs.check_print_servers_status(mxt::ServersInfo::default_repl_states());
        drop_client_user(test);
    }

    Client::drop_tables(test);
}
}

int main(int argc, char* argv[])
{
    TestConnections test;
    return test.run_test(argc, argv, run);
}
