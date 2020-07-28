/**
 * MXS-1713: SchemaRouter unable to process SHOW DATABASES for a lot of schemas
 *
 * https://jira.mariadb.org/browse/MXS-1713
 */
#include "testconnections.h"
#include <vector>
#include <set>
#include <numeric>

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);
    const int n_db = 2000;
    std::vector<std::string> db_list;

    for (int i = 0; i < n_db; i++)
    {
        db_list.push_back("db" + std::to_string(i));
    }

    test.tprintf("Create %lu databases...", db_list.size());
    test.repl->connect();
    for (auto db : db_list)
    {
        execute_query(test.repl->nodes[0], "CREATE DATABASE %s", db.c_str());
    }
    test.tprintf("Done!");

    test.tprintf("Opening a connection with each database as the default database...", db_list.size());
    std::set<std::string> errors;

    for (auto db : db_list)
    {
        MYSQL* conn = open_conn_db(test.maxscales->port(),
                                   test.maxscales->ip(),
                                   db,
                                   test.maxscales->user_name,
                                   test.maxscales->password);
        if (execute_query_silent(conn, "SELECT 1")
            || execute_query_silent(conn, "SHOW DATABASES"))
        {
            errors.insert(mysql_error(conn));
        }
        mysql_close(conn);
    }
    test.tprintf("Done!");

    auto combiner = [](std::string& a, std::string b) {
            a += b + " ";
            return a;
        };

    std::string errstr;
    std::accumulate(errors.begin(), errors.end(), errstr, combiner);
    test.expect(errors.empty(), "None of the queries should fail: %s", errstr.c_str());

    test.tprintf("Dropping databases...");
    for (auto db : db_list)
    {
        execute_query(test.repl->nodes[0], "DROP DATABASE %s", db.c_str());
    }
    test.tprintf("Done!");

    return test.global_result;
}
