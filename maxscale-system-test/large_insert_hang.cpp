/**
 * Check that the OK packet flags are read correctly
 */

#include "testconnections.h"

int main(int argc, char *argv[])
{
    TestConnections test(argc, argv);
    test.set_timeout(60);

    test.connect_maxscale();
    test.try_query(test.conn_rwsplit, "CREATE OR REPLACE TABLE test.t1(id int)");

    std::stringstream ss;
    ss << "INSERT INTO test.t1 VALUES (0)";

    for (int i = 0; i < 2299; i++)
    {
        ss << ",(" << i << ")";
    }

    test.try_query(test.conn_rwsplit, query.str().c_str());
    test.try_query(test.conn_rwsplit, "DROP TABLE test.t1");
    test.close_maxscale_connections();

    return test.global_result;
}
