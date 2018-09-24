/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2022-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <algorithm>
#include <iostream>
#include <iterator>
#include <sstream>
#include <maxbase/assert.h>
#include <maxbase/log.hh>
#include "testconnections.h"

using namespace std;

namespace
{

// The amount of time slept between various operations that are
// expected to take some time before becoming visible.

const int REPLICATION_SLEEP = 5; // Seconds

string get_gtid_current_pos(TestConnections& test, MYSQL* pMysql)
{
    std::vector<string> row = get_row(pMysql, "SELECT @@gtid_current_pos");

    test.expect(row.size() == 1, "Did not get @@gtid_current_pos");

    return row.empty() ? "" : row[0];
}

// Setup BLR to replicate from galera_000.
bool setup_blr(TestConnections& test, const string& gtid, const char* zHost, int port)
{
    test.tprintf("Connecting to BLR at %s:%d", zHost, port);

    MYSQL* pMysql = open_conn_no_db(port, zHost, "repl", "repl");
    test.expect(pMysql, "Could not open connection to %s.", zHost);

    test.try_query(pMysql, "STOP SLAVE");
    test.try_query(pMysql, "SET @@global.gtid_slave_pos='%s'", gtid.c_str());

    mxb_assert(test.galera);
    Mariadb_nodes& gc = *test.galera;

    stringstream ss;

    ss << "CHANGE MASTER ";
    ss << "TO MASTER_HOST='" << gc.IP[0] << "', ";
    ss << "MASTER_PORT=" << gc.port[0] << ", ";
    ss << "MASTER_USER='repl', MASTER_PASSWORD='repl', MASTER_USE_GTID=Slave_pos";

    string stmt = ss.str();

    cout << stmt << endl;

    test.try_query(pMysql, "%s", stmt.c_str());
    test.try_query(pMysql, "START SLAVE");

    mysql_close(pMysql);

    return test.global_result == 0;
}

// Setup slave to replicate from BLR.
bool setup_slave(TestConnections& test,
                 const string& gtid,
                 MYSQL* pSlave,
                 const char* zMaxscale_host,
                 int maxscale_port)
{
    test.try_query(pSlave, "SET GLOBAL server_id=54"); // Remove this when galera/ms server ids are distinct
    test.try_query(pSlave, "STOP SLAVE");
    test.try_query(pSlave, "RESET SLAVE");
    test.try_query(pSlave, "DROP TABLE IF EXISTS test.MXS2011");
    test.try_query(pSlave, "SET @@global.gtid_slave_pos='%s'", gtid.c_str());

    stringstream ss;

    ss << "CHANGE MASTER TO ";
    ss << "MASTER_HOST='" << zMaxscale_host << "', ";
    ss << "MASTER_PORT=" << maxscale_port << ", ";
    ss << "MASTER_USER='repl', MASTER_PASSWORD='repl', MASTER_USE_GTID=Slave_pos";

    string stmt = ss.str();

    cout << stmt << endl;

    test.try_query(pSlave, "%s", stmt.c_str());
    test.try_query(pSlave, "START SLAVE");

    return test.global_result == 0;
}

bool setup_schema(TestConnections& test, MYSQL* pServer)
{
    test.try_query(pServer, "DROP TABLE IF EXISTS test.MXS2011");
    test.try_query(pServer, "CREATE TABLE test.MXS2011 (i INT)");

    return test.global_result == 0;
}

unsigned count = 0;

void insert(TestConnections& test, MYSQL* pMaster)
{
    stringstream ss;
    ss << "INSERT INTO test.MXS2011 VALUES (" << ++count << ")";

    string stmt = ss.str();

    cout << stmt.c_str() << endl;

    test.try_query(pMaster, "%s", stmt.c_str());
}

void select(TestConnections& test, MYSQL* pSlave)
{
    my_ulonglong nRows;
    unsigned long long nResult_sets;

    int rc = execute_query_num_of_rows(pSlave, "SELECT * FROM test.MXS2011", &nRows, &nResult_sets);
    test.expect(rc == 0, "Execution of SELECT failed.");

    if (rc == 0)
    {
        mxb_assert(nResult_sets == 1);

        test.expect(nRows == count, "Expected %d rows, got %d.", count, (int)nRows);
    }
}

bool insert_select(TestConnections& test, MYSQL* pSlave, MYSQL* pMaster)
{
    insert(test, pMaster);
    sleep(REPLICATION_SLEEP); // To ensure that the insert reaches the slave.
    select(test, pSlave);

    return test.global_result == 0;
}

bool insert_select(TestConnections& test, MYSQL* pSlave)
{
    Mariadb_nodes& gc = *test.galera;

    for (int i = 0; i < gc.N; ++i)
    {
        MYSQL* pMaster = gc.nodes[i];

        insert_select(test, pSlave, pMaster);
    }

    return test.global_result == 0;
}

void reset_galera(TestConnections& test)
{
    Mariadb_nodes& gc = *test.galera;

    for (int i = 0; i < gc.N; ++i)
    {
        test.try_query(gc.nodes[i], "RESET MASTER");
    }
}

// Ensure log_slave_updates is on.
void setup_galera(TestConnections& test)
{
    Mariadb_nodes& gc = *test.galera;

    for (int i = 0; i < gc.N; ++i)
    {
        gc.stash_server_settings(i);
        gc.add_server_setting(i, "log_slave_updates=1");
    }
}

// Restore log_slave_updates as it was.
void restore_galera(TestConnections& test)
{
    Mariadb_nodes& gc = *test.galera;

    for (int i = 0; i < gc.N; ++i)
    {
        gc.restore_server_settings(i);
    }

    int rc = gc.start_replication();
    test.expect(rc == 0, "Could not start Galera cluster.");
}

// STOP SLAVE; START SLAVE cycle.
void restart_slave(TestConnections& test, MYSQL* pSlave)
{
    Row row;

    auto replication_failed = [] (const std::string& column)
        {
            return column.find("Got fatal error") != string::npos;
        };

    test.try_query(pSlave, "STOP SLAVE");

    row = get_row(pSlave, "SHOW SLAVE STATUS");
    auto it1 = std::find_if(row.begin(), row.end(), replication_failed);
    test.expect(it1 == row.end(), "Replication failed.");

    test.try_query(pSlave, "START SLAVE");

    sleep(REPLICATION_SLEEP);

    // The START SLAVE above fails if BLR does not handle GTIDs correctly.
    row = get_row(pSlave, "SHOW SLAVE STATUS");
    auto it2 = std::find_if(row.begin(), row.end(), replication_failed);
    test.expect(it2 == row.end(), "START SLAVE failed.");
}

}

int main(int argc, char* argv[])
{
    mxb::Log log(MXB_LOG_TARGET_STDOUT);

    TestConnections::skip_maxscale_start(false);
    TestConnections test(argc, argv);

    test.maxscales->ssh_node(0, "rm -f /var/lib/maxscale/master.ini", true);
    test.maxscales->ssh_node(0, "rm -f /var/lib/maxscale/gtid_maps.db", true);
    test.maxscales->ssh_node(0, "rm -rf /var/lib/maxscale/0", true);

    test.start_maxscale(0);

    setup_galera(test);
    test.galera->start_replication(); // Causes restart.

    Mariadb_nodes& gc = *test.galera;
    gc.connect();

    reset_galera(test);

    string gtid = get_gtid_current_pos(test, gc.nodes[0]);

    cout << "GTID: " << gtid << endl;

    const char* zValue;

    // Env-vars for debugging.
    zValue = getenv("MXS2047_BLR_HOST");
    const char* zMaxscale_host = (zValue ? zValue : test.maxscales->IP[0]);
    cout << "MaxScale host: " << zMaxscale_host << endl;

    zValue = getenv("MXS2047_BLR_PORT");
    int maxscale_port = (zValue ? atoi(zValue) : test.maxscales->binlog_port[0]);
    cout << "MaxScale port: " << maxscale_port << endl;

    if (setup_blr(test, gtid, zMaxscale_host, maxscale_port))
    {
        int slave_index = test.repl->N - 1; // We use the last slave.

        Mariadb_nodes& ms = *test.repl;
        ms.connect(slave_index);

        MYSQL* pSlave = ms.nodes[slave_index];
        mxb_assert(pSlave);

        if (setup_slave(test, gtid, pSlave, zMaxscale_host, maxscale_port))
        {
            if (setup_schema(test, gc.nodes[0]))
            {
                sleep(REPLICATION_SLEEP);

                if (insert_select(test, pSlave))
                {
                    restart_slave(test, pSlave);
                }
            }
        }
    }

    restore_galera(test);

    return test.global_result;
}
