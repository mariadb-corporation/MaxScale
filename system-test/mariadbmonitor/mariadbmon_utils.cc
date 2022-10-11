/*
 * Copyright (c) 2022 MariaDB Corporation Ab
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

#include "mariadbmon_utils.hh"
#include <maxbase/assert.hh>
#include <maxbase/format.hh>

using std::string;

namespace
{
enum class SyncMxs
{
    YES,
    NO
};

bool generate_traffic_and_check(TestConnections& test, mxt::MariaDB* conn, int insert_count, SyncMxs sync);
}

/**
 * Do inserts, check that results are as expected.
 *
 * @param test Test connections
 * @param conn Which specific connection to use
 * @param insert_count How many inserts should be done
 * @return True, if successful
 */
bool generate_traffic_and_check(TestConnections& test, mxt::MariaDB* conn, int insert_count)
{
    return generate_traffic_and_check(test, conn, insert_count, SyncMxs::YES);
}

bool generate_traffic_and_check_nosync(TestConnections& test, mxt::MariaDB* conn, int insert_count)
{
    return generate_traffic_and_check(test, conn, insert_count, SyncMxs::NO);
}

namespace
{
bool generate_traffic_and_check(TestConnections& test, mxt::MariaDB* conn, int insert_count, SyncMxs sync)
{
    const bool wait_sync = (sync == SyncMxs::YES);
    const char table[] = "test.t1";
    int inserts_start = 1;

    auto show_tables = conn->query("show tables from test like 't1';");
    if (show_tables && show_tables->next_row() && show_tables->get_string(0) == "t1")
    {
        auto res = conn->query_f("select count(*) from %s;", table);
        if (res && res->next_row())
        {
            inserts_start = res->get_int(0) + 1;
        }
    }
    else if (test.ok())
    {
        conn->cmd_f("create table %s(c1 int)", table);
    }

    bool ok = false;
    if (test.ok())
    {
        int inserts_end = inserts_start + insert_count;

        ok = true;
        for (int i = inserts_start; i <= inserts_end && ok; i++)
        {
            ok = conn->cmd_f("insert into %s values (%d);", table, i);
        }

        if (ok)
        {
            if (wait_sync)
            {
                test.sync_repl_slaves();
            }

            auto res = conn->query_f("SELECT * FROM %s;", table);
            if (res)
            {
                // Check all values, they should go from 1 to inserts_end
                int expected_val = 0;
                while (res->next_row() && ok)
                {
                    expected_val++;
                    auto value = res->get_int(0);
                    if (value != expected_val)
                    {
                        test.add_failure("Query returned %ld when %d was expected.", value, expected_val);
                        ok = false;
                    }
                }

                if (ok && expected_val != inserts_end)
                {
                    test.add_failure("Query returned %d rows when %d rows were expected.",
                                     expected_val, insert_count);
                    ok = false;
                }

                if (ok && wait_sync)
                {
                    // Wait for monitor to detect gtid change.
                    test.maxscale->wait_for_monitor();
                }
            }
            else
            {
                ok = false;
            }
        }
    }
    return ok;
}
}

void prepare_log_bin_failover_test(TestConnections& test)
{
    auto& mxs = *test.maxscale;
    auto& repl = *test.repl;
    mxs.stop_maxscale();

    repl.stop_node(1);
    repl.stash_server_settings(1);
    repl.disable_server_setting(1, "log-bin");
    repl.disable_server_setting(1, "log_bin");
    repl.start_node(1);

    repl.stop_node(2);
    repl.stash_server_settings(2);
    repl.disable_server_setting(2, "log-slave-updates");
    repl.disable_server_setting(2, "log_slave_updates");
    repl.start_node(2);

    mxs.start_maxscale();
    mxs.wait_for_monitor(1);
}

void cleanup_log_bin_failover_test(TestConnections& test)
{
    // Restore server2 and 3 settings.
    auto& repl = *test.repl;
    test.tprintf("Restoring server settings.");

    repl.stop_node(1);
    repl.restore_server_settings(1);
    repl.start_node(1);

    repl.stop_node(2);
    repl.restore_server_settings(2);
    repl.start_node(2);

    test.maxscale->wait_for_monitor(1);
}

namespace testclient
{
Client::Client(TestConnections& test, const Settings& sett, int id, bool verbose)
    : m_test(test)
    , m_settings(sett)
    , m_id(id)
    , m_verbose(verbose)
    , m_value(1)
    , m_rand_dist(0.0, 1.0)
{
}

void Client::start()
{
    m_keep_running = true;
    m_thread = std::thread(&Client::run, this);
}

void Client::stop()
{
    m_keep_running = false;
    m_thread.join();
}

Client::Action Client::action() const
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

bool Client::run_query(MYSQL* pConn)
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

bool Client::run_select(MYSQL* pConn)
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

bool Client::run_update(MYSQL* pConn)
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

void Client::flush_response(MYSQL* pConn)
{
    do
    {
        MYSQL_RES* pRes = mysql_store_result(pConn);
        mysql_free_result(pRes);
    }
    while (mysql_next_result(pConn) == 0);
}

int Client::get_random_id() const
{
    int id = m_settings.rows * random_decimal_fraction();

    mxb_assert(id >= 0);
    mxb_assert(id <= m_settings.rows);

    return id;
}

double Client::random_decimal_fraction() const
{
    return m_rand_dist(m_rand_gen);
}

void Client::run()
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

ClientGroup::ClientGroup(TestConnections& test, int nClients, Settings settings)
    : m_test(test)
    , m_nClients(nClients)
    , m_settings(std::move(settings))
{
}

void ClientGroup::prepare()
{
    if (create_tables())
    {
        if (insert_data())
        {
            m_test.repl->sync_slaves();
        }
    }
}

void ClientGroup::cleanup()
{
    m_test.tprintf("Dropping tables.");
    auto sConn = m_test.maxscale->open_rwsplit_connection2();

    for (int i = 0; i < m_nClients; ++i)
    {
        sConn->cmd_f("drop table test.t%d;", i);
    }
}

void ClientGroup::start()
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

void ClientGroup::stop()
{
    for (int i = 0; i < m_nClients; i++)
    {
        m_clients[i]->stop();
    }
    m_clients.clear();
}

bool ClientGroup::create_tables()
{
    m_test.tprintf("Creating tables.");
    auto sConn = m_test.maxscale->open_rwsplit_connection2();

    for (int i = 0; i < m_nClients; ++i)
    {
        sConn->cmd_f("create or replace table test.t%d (id int);", i);
    }

    return m_test.ok();
}

bool ClientGroup::insert_data()
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
}
