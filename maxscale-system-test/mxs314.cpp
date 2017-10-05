/**
 * @file mx314.cpp regression case for bug MXS-314 ("Read Write Split Error with Galera Nodes")
 * - try prepared stmt 'SELECT 1,1,1,1...." with different nu,ber of '1'
 * - check if Maxscale alive
 */


#include <iostream>
#include <unistd.h>
#include <cstdlib>
#include <string>
#include "testconnections.h"

using std::string;
using namespace std;

int main(int argc, char *argv[])
{
    MYSQL_STMT* stmt;
    int start = 300, p = 0;
    int iterations = 2000;
    string query = "select 1";

    TestConnections * Test = new TestConnections(argc, argv);
    if (Test->smoke)
    {
        iterations = 500;
    }
    Test->set_timeout(50);

    Test->connect_maxscale();

    stmt = mysql_stmt_init(Test->maxscales->conn_rwsplit[0]);

    for (int i = 0; i < start; i++)
    {
        query += ",1";
    }

    Test->tprintf("Query: %s\n", query.c_str());

    for (int i = start; i < iterations; i++)
    {
        Test->set_timeout(30);
        Test->tprintf("%d\t", i);
        if (mysql_stmt_prepare(stmt, query.c_str(), query.length()))
        {
            Test->add_result(1, "Error: %s\n", mysql_error(Test->maxscales->conn_rwsplit[0]));
            Test->add_result(1, "Failed at %d\n", i);
//            delete Test;
//            return 1;
        }
        if (mysql_stmt_reset(stmt))
        {
            Test->add_result(1, "Error: %s\n", mysql_error(Test->maxscales->conn_rwsplit[0]));
            Test->add_result(1, "Failed at %d\n", i);
//            delete Test;
//            return 1;
        }
        query += ",1";
        if (i - p > 5)
        {
            p = i;
            cout << endl;
        }
    }
    cout << endl;
    Test->set_timeout(20);
    mysql_stmt_close(stmt);
    Test->close_maxscale_connections();
    int rval = Test->global_result;
    delete Test;
    return rval;
}
