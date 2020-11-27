/**
 * MXS-2490: Unknown prepared statement handler (0) given to mysqld_stmt_execute
 *
 * See:
 *
 * https://mariadb.com/kb/en/library/mariadb_stmt_execute_direct/
 * https://mariadb.com/kb/en/library/com_stmt_execute/#statement-id
 */

#include <maxtest/testconnections.hh>

void run_test(TestConnections& test, MYSQL* conn)
{
    MYSQL_STMT* stmt = mysql_stmt_init(conn);
    std::string query = "SELECT user FROM mysql.user";

    for (int i = 0; i < 10 && test.ok(); i++)
    {
        test.expect(mariadb_stmt_execute_direct(stmt, query.c_str(), query.length()) == 0,
                    "execute_direct should work: %s", mysql_stmt_error(stmt));
    }

    mysql_stmt_close(stmt);
}

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);

    test.set_timeout(30);
    test.maxscales->connect();

    test.tprintf("Testing readwritesplit");
    run_test(test, test.maxscales->conn_rwsplit[0]);
    test.tprintf("Testing readconnroute");
    run_test(test, test.maxscales->conn_master[0]);

    return test.global_result;
}
