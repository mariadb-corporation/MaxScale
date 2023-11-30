/*
 * Copyright (c) 2022 MariaDB Corporation Ab
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

#pragma once

#include <maxtest/testconnections.hh>
#include <string>
#include <random>
#include <iostream>

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

    bool create_table(mxt::MariaDB& conn);
    bool drop_table(mxt::MariaDB& conn);
    void start();
    void stop();

    int id() const;

    struct Stats
    {
        int selects_good {0};   /**< Selects that succeeded and gave expected answer. */
        int selects_bad {0};    /**< Selects that failed or gave wrong answer. */
        int updates_good {0};   /**< Successful updates */
        int updates_bad {0};    /**< Failed updates */

        int trx_good {0};
        int trx_selects_bad {0};
        int trx_updates_bad {0};

        Stats& operator+=(const Stats& rhs);
    };
    Stats stats() const;

private:
    bool run_query(mxt::MariaDB& conn);
    bool run_select(mxt::MariaDB& conn);
    bool run_update(mxt::MariaDB& conn);
    bool run_trx(mxt::MariaDB& conn);
    void run();

private:
    const int        m_id {-1};
    TestConnections& m_test;
    const Settings&  m_settings;
    bool             m_verbose;

    std::string      m_tbl;
    std::thread      m_thread;
    std::atomic_bool m_keep_running {true};

    std::vector<int> m_values;      /**< The values the table should have */

    std::mt19937                       m_rand_gen;
    std::uniform_int_distribution<int> m_row_gen;
    std::uniform_int_distribution<int> m_val_gen;
    std::uniform_int_distribution<int> m_action_gen;

    Stats m_stats;
};

class ClientGroup
{
public:
    ClientGroup(TestConnections& test, int nClients, Settings settings);

    bool prepare();
    void cleanup();
    void start();
    void stop();

    Client::Stats total_stats() const;
    void          print_stats();

private:
    TestConnections&                     m_test;
    std::vector<std::unique_ptr<Client>> m_clients;
    const int                            m_nClients {0};
    const Settings                       m_settings;

    bool create_tables();
};
}

namespace stress_test
{
struct BaseSettings
{
    time_t test_duration {0};
    int    test_clients {0};
    int    min_expected_failovers {-1};
    bool   diverging_allowed {false};
};

void run_failover_stress_test(TestConnections& test, const BaseSettings& base_sett,
                              const testclient::Settings& client_sett);

void check_semisync_off(TestConnections& test);
void check_semisync_status(TestConnections& test, int node, bool master, bool slave, int expected_clients);
}
