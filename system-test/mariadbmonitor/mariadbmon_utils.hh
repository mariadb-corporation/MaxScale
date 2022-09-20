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

#pragma once

#include <maxtest/testconnections.hh>
#include <string>
#include <random>

bool generate_traffic_and_check(TestConnections& test, mxt::MariaDB* conn, int insert_count);
bool generate_traffic_and_check_nosync(TestConnections& test, mxt::MariaDB* conn, int insert_count);

void prepare_log_bin_failover_test(TestConnections& test);
void cleanup_log_bin_failover_test(TestConnections& test);

namespace testclient
{

struct Settings
{
    std::string host;
    int         port {0};
    std::string user;
    std::string pw;
    int         rows {0};
};

class Client
{
public:
    Client(TestConnections& test, const Settings& sett, int id, bool verbose);

    void start();
    void stop();

private:
    enum class Action
    {
        SELECT,
        UPDATE
    };

    Action action() const;

    bool run_query(MYSQL* pConn);

    bool run_select(MYSQL* pConn);

    bool run_update(MYSQL* pConn);

    static void flush_response(MYSQL* pConn);

    int get_random_id() const;

    double random_decimal_fraction() const;

    void run();

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
    ClientGroup(TestConnections& test, int nClients, Settings settings);

    void prepare();
    void cleanup();
    void start();
    void stop();

private:
    TestConnections&                     m_test;
    std::vector<std::unique_ptr<Client>> m_clients;
    const int                            m_nClients {0};
    const Settings                       m_settings;

    bool create_tables();
    bool insert_data();
};
}
