/**
 * Check that using no password returns correct error message
 */

#include <maxtest/testconnections.hh>

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);

    auto mxs_ip = test.maxscales->ip4();
    MYSQL* mysql = open_conn(test.maxscales->rwsplit_port, mxs_ip, "testuser", "", false);
    test.add_result(mysql_errno(mysql) == 0, "Connecting to MaxScale should fail");
    test.add_result(strstr(mysql_error(mysql), "using password: NO") == NULL,
                    "Missing (using password: NO) error message, got this instead: %s",
                    mysql_error(mysql));
    test.tprintf("MySQL error: %s", mysql_error(mysql));
    mysql_close(mysql);

    mysql = open_conn(test.maxscales->rwsplit_port, mxs_ip, "testuser", "testpassword", false);
    test.add_result(mysql_errno(mysql) == 0, "Connecting to MaxScale should fail");
    test.add_result(strstr(mysql_error(mysql), "using password: YES") == NULL,
                    "Missing (using password: YES) error message, got this instead: %s",
                    mysql_error(mysql));
    test.tprintf("MySQL error: %s", mysql_error(mysql));
    mysql_close(mysql);

    return test.global_result;
}
