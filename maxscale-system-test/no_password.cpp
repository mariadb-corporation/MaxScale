/**
 * Check that using no password returns correct error message
 */

#include "testconnections.h"

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);

    MYSQL* mysql = open_conn(test.maxscales->rwsplit_port[0], test.maxscales->IP[0], "testuser", "", false);
    test.add_result(mysql_errno(mysql) == 0, "Connecting to MaxScale should fail");
    test.add_result(strstr(mysql_error(mysql), "using password: NO") == NULL,
                    "Missing (using password: NO) error message, got this instead: %s",
                    mysql_error(mysql));
    test.tprintf("MySQL error: %s", mysql_error(mysql));
    mysql_close(mysql);

    open_conn(test.maxscales->rwsplit_port[0], test.maxscales->IP[0], "testuser", "testpassword", false);
    test.add_result(mysql_errno(mysql) == 0, "Connecting to MaxScale should fail");
    test.add_result(strstr(mysql_error(mysql), "using password: YES") == NULL,
                    "Missing (using password: YES) error message, got this instead: %s",
                    mysql_error(mysql));
    test.tprintf("MySQL error: %s", mysql_error(mysql));
    mysql_close(mysql);

    return test.global_result;
}
