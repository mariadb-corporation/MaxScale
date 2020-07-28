/**
 * MXS-2111: The password is stored in `authentication_string` instead of `password` due to MDEV-16774
 */

#include "testconnections.h"

int main(int argc, char** argv)
{
    TestConnections::require_repl_version("10.2.0");
    TestConnections test(argc, argv);

    auto batch = [&](std::vector<std::string> queries) {
            test.maxscales->connect();
            for (const auto& a : queries)
            {
                test.try_query(test.maxscales->conn_rwsplit[0], "%s", a.c_str());
            }
            test.maxscales->disconnect();
        };

    batch({"CREATE USER 'test' IDENTIFIED BY 'test'",
           "GRANT SELECT ON *.* TO test",
           "SET PASSWORD FOR 'test' = PASSWORD('test')"});

    MYSQL* conn = open_conn(test.maxscales->rwsplit_port[0], test.maxscales->IP[0], "test", "test");
    test.try_query(conn, "SELECT 1");
    mysql_close(conn);

    batch({"DROP USER 'test'"});

    return test.global_result;
}
