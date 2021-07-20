/**
 * MXS-1713: SchemaRouter unable to process SHOW DATABASES for a lot of schemas
 *
 * https://jira.mariadb.org/browse/MXS-1713
 */
#include <maxtest/testconnections.hh>
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
    test.repl->sync_slaves();
    test.tprintf("Done!");

    test.tprintf("Opening a connection with each database as the default database...");
    std::set<std::string> errors;
    int i = 0;

    for (auto db : db_list)
    {
        MYSQL* conn = open_conn_db(test.maxscale->port(),
                                   test.maxscale->ip(),
                                   db,
                                   test.maxscale->user_name,
                                   test.maxscale->password);

        test.expect(execute_query(conn, "SELECT 1") == 0, "Query should work: %s", mysql_error(conn));

        if (i++ % 300 == 0)
        {
            test.expect(execute_query(conn, "SHOW DATABASES") == 0, "Query should work: %s", mysql_error(conn));
        }

        mysql_close(conn);

        if (test.global_result)
        {
            break;
        }
    }
    test.tprintf("Done!");

    test.tprintf("Dropping databases...");
    for (auto db : db_list)
    {
        execute_query(test.repl->nodes[0], "DROP DATABASE %s", db.c_str());
    }
    test.tprintf("Done!");

    return test.global_result;
}
