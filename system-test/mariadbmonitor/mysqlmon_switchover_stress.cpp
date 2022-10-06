/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-10-04
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
    struct Settings
    {
        string host;
        int    port {0};
        string user;
        string pw;
        int    rows {0};
    };

    Client(const Settings& sett, int id, bool verbose)
        : m_id(id)
        , m_verbose(verbose)
        , m_value(1)
        , m_settings(sett)
        , m_rand_dist(0.0, 1.0)
    {
    }

    void start()
    {
        m_keep_running = true;
        m_thread = std::thread(&Client::run, this);
    }

    void stop()
    {
        m_keep_running = false;
        m_thread.join();
    }


    static void init(TestConnections& test, size_t nClients, size_t nRows)
    {
        if (create_tables(test, nClients))
        {
            if (insert_data(test, nClients, nRows))
            {
                cout << "\nSyncing slaves." << endl;
                test.repl->sync_slaves();
            }
        }
    }

    static bool drop_tables(TestConnections& test, int n_clients)
    {
        test.tprintf("Dropping tables.");
        auto sConn = test.maxscale->open_rwsplit_connection2();

        for (int i = 0; i < n_clients; ++i)
        {
            sConn->cmd_f("drop table test.t%d;", i);
        }

        return test.ok();
    }

private:
    enum class action_t
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
            return action_t::ACTION_UPDATE;
        }
        else
        {
            return action_t::ACTION_SELECT;
        }
    }

    bool run_query(MYSQL* pConn)
    {
        bool rv = false;

        switch (action())
        {
        case action_t::ACTION_SELECT:
            rv = run_select(pConn);
            break;

        case action_t::ACTION_UPDATE:
            rv = run_update(pConn);
            break;
        }

        return rv;
    }

    bool run_select(MYSQL* pConn)
    {
        bool rv = true;

        string stmt = mxb::string_printf("SELECT * FROM test.t%zu WHERE id=%i;",
                                         m_id, get_random_id());

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

        string stmt = mxb::string_printf("UPDATE test.t%zu SET id=%zu WHERE id=%i;",
                                         m_id, m_value, get_random_id());
        m_value = (m_value + 1) % m_settings.rows;

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
        int id = m_settings.rows * random_decimal_fraction();

        ss_dassert(id >= 0);
        ss_dassert(id <= m_settings.rows);

        return id;
    }

    double random_decimal_fraction() const
    {
        return m_rand_dist(m_rand_gen);
    }

    void run()
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

                if (mysql_real_connect(pMysql, m_settings.host.c_str(), m_settings.user.c_str(),
                                       m_settings.pw.c_str(), "test", m_settings.port, NULL, 0))
                {
                    if (m_verbose)
                    {
                        CMESSAGE("Connected.");
                    }

                    while (m_keep_running && run_query(pMysql))
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
        while (m_keep_running);
    }

    static bool create_tables(TestConnections& test, int n_clients)
    {
        test.tprintf("Creating tables.");
        auto sConn = test.maxscale->open_rwsplit_connection2();

        for (int i = 0; i < n_clients; ++i)
        {
            sConn->cmd_f("create or replace table test.t%d (id int);", i);
        }

        return test.ok();
    }

    static bool insert_data(TestConnections& test, int n_clients, int n_rows)
    {
        test.tprintf("Inserting data.");

        auto pConn = test.maxscale->open_rwsplit_connection2();

        for (int i = 0; i < n_clients; ++i)
        {
            string insert = mxb::string_printf("insert into test.t%d values ", i);

            for (int j = 0; j < n_rows; ++j)
            {
                insert += "(";
                insert += std::to_string(j);
                insert += ")";

                if (j < n_rows - 1)
                {
                    insert += ", ";
                }
            }

            pConn->cmd(insert);
        }

        return test.ok();
    }

private:

    size_t           m_id;
    bool             m_verbose;
    size_t           m_value;
    std::thread      m_thread;
    std::atomic_bool m_keep_running {true};

    const Settings&                                m_settings;
    mutable std::mt19937                           m_rand_gen;
    mutable std::uniform_real_distribution<double> m_rand_dist;
};
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

    const int n_clients = 4;
    const int n_rows = 100;
    Client::init(test, n_clients, n_rows);

    if (test.ok())
    {
        Client::Settings sett;
        sett.host = mxs.ip4();
        sett.port = mxs.rwsplit_port;
        sett.user = CLIENT_USER;
        sett.pw = CLIENT_PASSWORD;
        sett.rows = n_rows;

        test.tprintf("Starting %i clients. Connecting to %s:%i as '%s':'%s'.",
                     n_clients, sett.host.c_str(), sett.port, sett.user.c_str(), sett.pw.c_str());

        std::vector<std::unique_ptr<Client>> clients;
        for (int i = 0; i < n_clients; i++)
        {
            auto new_client = std::make_unique<Client>(sett, i, test.verbose());
            new_client->start();
            clients.emplace_back(std::move(new_client));
        }

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

        for (int i = 0; i < n_clients; i++)
        {
            clients[i]->stop();
        }

        // Ensure master is at server1. Shortens startup time for next test.
        if (current_master_id != 1)
        {
            switchover(test, 1, current_master_id);
        }

        mxs.check_print_servers_status(mxt::ServersInfo::default_repl_states());
        drop_client_user(test);
    }

    Client::drop_tables(test, n_clients);
}
}

int main(int argc, char* argv[])
{
    TestConnections test;
    return test.run_test(argc, argv, run);
}
