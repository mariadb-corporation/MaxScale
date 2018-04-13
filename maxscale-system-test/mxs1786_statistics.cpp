/**
 * MXS-1786: Hang with COM_STATISTICS
 */

#include "testconnections.h"

int main(int argc, char* argv[])
{
    TestConnections test(argc, argv);

    test.maxscales->connect();

    for (int i = 0; i < 10; i++)
    {
        test.set_timeout(10);
        mysql_stat(test.maxscales->conn_rwsplit[0]);
        test.try_query(test.maxscales->conn_rwsplit[0], "SELECT 1");
    }

    test.maxscales->disconnect();


    return test.global_result;
}
