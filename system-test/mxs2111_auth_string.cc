/**
 * MXS-2111: The password is stored in `authentication_string` instead of `password` due to MDEV-16774
 */

#include <maxtest/testconnections.hh>

int main(int argc, char** argv)
{
    TestConnections::require_repl_version("10.2.0");
    TestConnections test(argc, argv);

    auto batch = [&](std::vector<std::string> queries) {
            test.maxscale->connect();
            for (const auto& a : queries)
            {
                test.try_query(test.maxscale->conn_rwsplit, "%s", a.c_str());
            }
            test.maxscale->disconnect();
        };

    batch({"DROP USER IF EXISTS 'test'",
           "CREATE USER 'test' IDENTIFIED BY 'test'",
           "GRANT SELECT ON *.* TO test",
           "SET PASSWORD FOR 'test' = PASSWORD('test')"});

    MYSQL* conn = open_conn(test.maxscale->rwsplit_port, test.maxscale->ip4(), "test", "test");
    test.try_query(conn, "SELECT 1");
    mysql_close(conn);

    batch({"DROP USER 'test'"});

    return test.global_result;
}
