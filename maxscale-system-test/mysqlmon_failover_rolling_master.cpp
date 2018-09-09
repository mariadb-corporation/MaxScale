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

#include <iostream>
#include <string>
#include <sstream>
#include "testconnections.h"

using std::cerr;
using std::cout;
using std::endl;
using std::flush;
using std::string;
using std::stringstream;

namespace
{

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

int get_server_id(Maxscales& maxscales)
{
    MYSQL* conn = maxscales.open_rwsplit_connection(0);
    int id = -1;
    char str[1024];

    if (find_field(conn, "SELECT @@server_id, @@last_insert_id;", "@@server_id", str) == 0)
    {
        id = atoi(str);
    }

    mysql_close(conn);

    if (id == -1)
    {
        throw std::runtime_error("Could not get server id.");
    }

    return id;
}
}

namespace
{

class XTestConnections : private TestConnections
{
public:
    using TestConnections::add_result;
    using TestConnections::global_result;
    using TestConnections::maxscales;
    using TestConnections::repl;

    XTestConnections(int argc, char* argv[])
        : TestConnections(argc, argv)
    {
    }

    TestConnections& nothrow()
    {
        return *this;
    }

    void connect_maxscale(int m = 0)
    {
        if (maxscales->connect_maxscale(m) != 0)
        {
            ++global_result;
            throw std::runtime_error("Could not connect to MaxScale.");
        }
    }

    void try_query(MYSQL* conn, const char* format, ...)
    {
        va_list valist;

        va_start(valist, format);
        int message_len = vsnprintf(NULL, 0, format, valist);
        va_end(valist);

        char sql[message_len + 1];

        va_start(valist, format);
        vsnprintf(sql, sizeof(sql), format, valist);
        va_end(valist);

        int res = execute_query_silent(conn, sql, false);
        add_result(res,
                   "Query '%.*s%s' failed!\n",
                   message_len < 100 ? message_len : 100,
                   sql,
                   message_len < 100 ? "" : "...");

        if (res != 0)
        {
            string s("Could not execute query: ");
            s += sql;

            if (s.length() > 80)
            {
                s = s.substr(0, 77);
                s += "...";
            }

            throw std::runtime_error(s.c_str());
        }
    }
};

void list_servers(XTestConnections& test)
{
    cout << endl;
    test.maxscales->execute_maxadmin_command_print(0, (char*)"list servers");
}
}

namespace
{

void create_table(XTestConnections& test)
{
    test.try_query(test.maxscales->conn_rwsplit[0], "DROP TABLE IF EXISTS test.t1");
    test.try_query(test.maxscales->conn_rwsplit[0], "CREATE TABLE test.t1(id INT)");
}

static int i_start = 0;
static int n_rows = 20;
static int i_end = 0;

void insert_data(XTestConnections& test)
{
    test.try_query(test.maxscales->conn_rwsplit[0], "BEGIN");

    i_end = i_start + n_rows;

    for (int i = i_start; i < i_end; ++i)
    {
        stringstream ss;
        ss << "INSERT INTO test.t1 VALUES (" << i << ")";
        test.try_query(test.maxscales->conn_rwsplit[0], ss.str().c_str());
    }
    test.try_query(test.maxscales->conn_rwsplit[0], "COMMIT");

    i_start = i_end;
}

void check(XTestConnections& test)
{
    MYSQL* pConn = test.maxscales->open_rwsplit_connection(0);
    const char* zQuery = "SELECT * FROM test.t1";

    test.try_query(pConn, "BEGIN");
    mysql_query(pConn, zQuery);

    MYSQL_RES* pRes = mysql_store_result(pConn);
    test.add_result(pRes == NULL, "Query should return a result set.");


    if (!pRes)
    {
        mysql_close(pConn);
        throw std::runtime_error("Query did not return a result set.");
    }

    std::string values;
    int num_rows = mysql_num_rows(pRes);
    test.add_result(num_rows != i_end,
                    "Query returned %d rows when %d rows were expected",
                    num_rows,
                    i_end);
    test.nothrow().try_query(pConn, "COMMIT");
    mysql_close(pConn);
}

void stop_node(XTestConnections& test, int index)
{
    if (test.repl->stop_node(index) != 0)
    {
        throw std::runtime_error("Could not stop node.");
    }

    list_servers(test);
}

void run(XTestConnections& test)
{
    test.maxscales->wait_for_monitor();

    int N = test.repl->N;
    cout << "Nodes: " << N << endl;

    cout << "\nConnecting to MaxScale." << endl;
    test.connect_maxscale();

    cout << "\nCreating table." << endl;
    create_table(test);

    list_servers(test);

    for (int i = 0; i < N - 1; ++i)
    {
        cout << "Round: " << i << "\n"
             << "--------" << endl;
        cout << "\nInserting data." << endl;
        insert_data(test);

        cout << "\nSyncing slaves." << endl;
        test.repl->sync_slaves();

        int master_id = get_server_id(*test.maxscales);
        int master_index = master_id - 1;

        cout << "\nStopping master." << endl;
        stop_node(test, master_index);

        cout << "\nClosing connection to MaxScale." << endl;
        test.maxscales->close_maxscale_connections(0);

        test.maxscales->wait_for_monitor();

        list_servers(test);

        master_id = get_server_id(*test.maxscales);
        cout << "\nNew master is: " << master_id << endl;

        cout << "\nConnecting to MaxScale." << endl;
        test.connect_maxscale();

        cout << "\nChecking result." << endl;
        check(test);
    }
}
}

int main(int argc, char* argv[])
{
    Mariadb_nodes::require_gtid(true);
    XTestConnections test(argc, argv);

    try
    {
        run(test);
    }
    catch (const std::exception& x)
    {
        cerr << "error: Execution was terminated due to an exception: " << x.what() << endl;
    }

    return test.global_result;
}
