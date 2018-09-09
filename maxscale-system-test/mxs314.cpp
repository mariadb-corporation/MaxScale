/**
 * @file mx314.cpp regression case for bug MXS-314 ("Read Write Split Error with Galera Nodes")
 * - try prepared stmt 'SELECT 1,1,1,1...." with different number of '1'
 * - check if Maxscale alive
 */

#include "testconnections.h"

using namespace std;

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);
    string query = "select 1";

    for (int i = 0; i < 300; i++)
    {
        query += ",1";
    }

    test.maxscales->connect();

    MYSQL_STMT* stmt = mysql_stmt_init(test.maxscales->conn_rwsplit[0]);

    for (int i = 300; i < 500; i++)
    {
        test.set_timeout(30);
        test.add_result(mysql_stmt_prepare(stmt, query.c_str(), query.length()),
                        "Failed at %d: %s\n",
                        i,
                        mysql_error(test.maxscales->conn_rwsplit[0]));
        test.add_result(mysql_stmt_reset(stmt),
                        "Failed at %d: %s\n",
                        i,
                        mysql_error(test.maxscales->conn_rwsplit[0]));

        query += ",1";
    }

    test.set_timeout(20);
    mysql_stmt_close(stmt);
    test.maxscales->disconnect();

    return test.global_result;
}
