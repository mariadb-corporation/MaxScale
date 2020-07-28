/**
 * MXS-2490: Unknown prepared statement handler (0) given to mysqld_stmt_execute
 *
 * See:
 *
 * https://mariadb.com/kb/en/library/mariadb_stmt_execute_direct/
 * https://mariadb.com/kb/en/library/com_stmt_execute/#statement-id
 */

#include "testconnections.h"

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);

    test.set_timeout(30);
    test.maxscales->connect();

    MYSQL_STMT* stmt = mysql_stmt_init(test.maxscales->conn_rwsplit[0]);
    std::string query = "SELECT user FROM mysql.user";
    test.expect(mariadb_stmt_execute_direct(stmt, query.c_str(), query.length()) == 0,
                "execute_direct should work: %s", mysql_stmt_error(stmt));
    mysql_stmt_close(stmt);

    return test.global_result;
}
