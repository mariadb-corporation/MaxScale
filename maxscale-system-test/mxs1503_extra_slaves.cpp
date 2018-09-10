/**
 * MXS-1503: Make sure no extra slaves are taken into use
 *
 * https://jira.mariadb.org/browse/MXS-1503
 */
#include "testconnections.h"
#include <vector>
#include <thread>
#include <iostream>

void query(MYSQL* conn, std::string q)
{
    execute_query(conn, "%s", q.c_str());
    mysql_close(conn);
}

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);
    std::vector<std::thread> connections;

    test.maxscales->connect();

    Row original_row = get_row(test.maxscales->conn_rwsplit[0], "SELECT @@server_id");

    for (int i = 0; i < 10; i++)
    {
        connections.emplace_back(query, test.maxscales->open_rwsplit_connection(), "SELECT SLEEP(10)");
        sleep(1);
        Row row = get_row(test.maxscales->conn_rwsplit[0], "SELECT @@server_id");
        test.expect(row == original_row, "Value of @@server_id should not change: %s", row.at(0).c_str());
    }

    for (auto& a : connections)
    {
        a.join();
        Row row = get_row(test.maxscales->conn_rwsplit[0], "SELECT @@server_id");
        test.expect(row == original_row, "Value of @@server_id should not change: %s", row.at(0).c_str());
    }


    test.maxscales->disconnect();

    return test.global_result;
}
