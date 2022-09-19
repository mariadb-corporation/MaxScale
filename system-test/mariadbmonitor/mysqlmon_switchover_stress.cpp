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

#include <maxtest/testconnections.hh>
#include <string>
#include <thread>
#include <vector>
#include <random>
#include <maxbase/assert.h>
#include <maxbase/format.hh>

using std::string;

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

    Client(TestConnections& test, const Settings& sett, int id, bool verbose)
        : m_test(test)
        , m_settings(sett)
        , m_id(id)
        , m_verbose(verbose)
        , m_value(1)
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

private:
    enum class Action
    {
        SELECT,
        UPDATE
    };

    Action action() const
    {
        double d = random_decimal_fraction();

        // 20% updates
        // 80% selects
        if (d <= 0.2)
        {
            return Action::UPDATE;
        }
        else
        {
            return Action::SELECT;
        }
    }

    bool run_query(MYSQL* pConn)
    {
        bool rv = false;

        switch (action())
        {
        case Action::SELECT:
            rv = run_select(pConn);
            break;

        case Action::UPDATE:
            rv = run_update(pConn);
            break;
        }

        return rv;
    }

    bool run_select(MYSQL* pConn)
    {
        bool rv = true;

        string stmt = mxb::string_printf("SELECT * FROM test.t%d WHERE id=%i;",
                                         m_id, get_random_id());

        if (mysql_query(pConn, stmt.c_str()) == 0)
        {
            flush_response(pConn);
        }
        else
        {
            if (m_verbose)
            {
                m_test.tprintf("\"%s\" failed: %s", stmt.c_str(), mysql_error(pConn));
            }
            rv = false;
        }

        return rv;
    }

    bool run_update(MYSQL* pConn)
    {
        bool rv = true;

        string stmt = mxb::string_printf("UPDATE test.t%d SET id=%zu WHERE id=%i;",
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
                m_test.tprintf("\"%s\" failed: %s", stmt.c_str(), mysql_error(pConn));
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

        mxb_assert(id >= 0);
        mxb_assert(id <= m_settings.rows);

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

                if (mysql_real_connect(pMysql, m_settings.host.c_str(), m_settings.user.c_str(),
                                       m_settings.pw.c_str(), "test", m_settings.port, NULL, 0))
                {
                    if (m_verbose)
                    {
                        m_test.tprintf("Client %i connected, starting queries.", m_id);
                    }

                    while (m_keep_running && run_query(pMysql))
                    {
                    }
                }
                else
                {
                    if (m_verbose)
                    {
                        m_test.tprintf("mysql_real_connect() on client %i failed: %s",
                                       m_id, mysql_error(pMysql));
                    }
                }

                mysql_close(pMysql);
                if (m_verbose)
                {
                    m_test.tprintf("Client %i connection closed.", m_id);
                }
            }
            else
            {
                m_test.tprintf("mysql_init() failed on client %i.", m_id);
            }

            // To prevent some backend from becoming overwhelmed.
            sleep(1);
        }
        while (m_keep_running);
    }

private:
    TestConnections& m_test;
    const Settings&  m_settings;

    int              m_id {-1};
    bool             m_verbose;
    size_t           m_value;
    std::thread      m_thread;
    std::atomic_bool m_keep_running {true};

    mutable std::mt19937                           m_rand_gen;
    mutable std::uniform_real_distribution<double> m_rand_dist;
};

class ClientGroup
{
public:
    ClientGroup(TestConnections& test, int nClients, Client::Settings settings)
        : m_test(test)
        , m_nClients(nClients)
        , m_settings(std::move(settings))
    {
    }

    void prepare()
    {
        if (create_tables())
        {
            if (insert_data())
            {
                m_test.repl->sync_slaves();
            }
        }
    }

    void cleanup()
    {
        m_test.tprintf("Dropping tables.");
        auto sConn = m_test.maxscale->open_rwsplit_connection2();

        for (int i = 0; i < m_nClients; ++i)
        {
            sConn->cmd_f("drop table test.t%d;", i);
        }
    }

    void start()
    {
        m_test.tprintf("Starting %i clients. Connecting to %s:%i as '%s'.",
                       m_nClients, m_settings.host.c_str(), m_settings.port, m_settings.user.c_str());

        for (int i = 0; i < m_nClients; i++)
        {
            auto new_client = std::make_unique<Client>(m_test, m_settings, i, m_test.verbose());
            new_client->start();
            m_clients.emplace_back(std::move(new_client));
        }
    }

    void stop()
    {
        for (int i = 0; i < m_nClients; i++)
        {
            m_clients[i]->stop();
        }
        m_clients.clear();
    }

private:
    TestConnections&                     m_test;
    std::vector<std::unique_ptr<Client>> m_clients;
    const int                            m_nClients {0};
    const Client::Settings               m_settings;

    bool create_tables()
    {
        m_test.tprintf("Creating tables.");
        auto sConn = m_test.maxscale->open_rwsplit_connection2();

        for (int i = 0; i < m_nClients; ++i)
        {
            sConn->cmd_f("create or replace table test.t%d (id int);", i);
        }

        return m_test.ok();
    }

    bool insert_data()
    {
        m_test.tprintf("Inserting data.");

        auto pConn = m_test.maxscale->open_rwsplit_connection2();

        for (int i = 0; i < m_nClients; ++i)
        {
            string insert = mxb::string_printf("insert into test.t%d values ", i);

            for (int j = 0; j < m_settings.rows; ++j)
            {
                insert += "(";
                insert += std::to_string(j);
                insert += ")";

                if (j < m_settings.rows - 1)
                {
                    insert += ", ";
                }
            }

            pConn->cmd(insert);
        }

        return m_test.ok();
    }
};

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

    Client::Settings sett;
    sett.host = mxs.ip4();
    sett.port = mxs.rwsplit_port;
    sett.user = CLIENT_USER;
    sett.pw = CLIENT_PASSWORD;
    sett.rows = 100;
    ClientGroup clients(test, 4, sett);
    clients.prepare();

    if (test.ok())
    {
        clients.start();

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

        clients.stop();

        // Ensure master is at server1. Shortens startup time for next test.
        if (current_master_id != 1)
        {
            switchover(test, 1, current_master_id);
        }

        mxs.check_print_servers_status(mxt::ServersInfo::default_repl_states());
        drop_client_user(test);
    }

    clients.cleanup();
}
}

int main(int argc, char* argv[])
{
    TestConnections test;
    return test.run_test(argc, argv, run);
}
