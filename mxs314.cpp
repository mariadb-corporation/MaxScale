/**
 * @file mx314.cpp regression case for bug MXS-314 ("")
 *
 * - check if Maxscale alive
 */

#include <my_config.h>
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
    string query = "select 1";

    TestConnections * Test = new TestConnections(argc, argv);
    Test->print_env();
    Test->connect_maxscale();

    stmt = mysql_stmt_init(Test->conn_rwsplit);

    for(int i = 0;i<start;i++)
        query += ",1";

    printf("Query: %s\n", query.c_str());

    for(int i = start;i<1000;i++)
    {
        cout << i << " ";
        if(mysql_stmt_prepare(stmt,query.c_str(),query.length()))
        {
            cout << "Error: " << mysql_error(Test->conn_rwsplit) << endl;
            cout << "Failed at " << i << endl;
            return 1;
        }
        if(mysql_stmt_reset(stmt))
        {
            cout << "Error: " << mysql_error(Test->conn_rwsplit) << endl;
            cout << "Failed at " << i << endl;
            return 1;
        }
        query += ",1";
        if(i - p > 20)
        {
            p = i;
            cout << endl;
        }
    }
    cout << endl;
    mysql_stmt_close(stmt);
    mysql_close(Test->conn_rwsplit);
    return 0;
}
