/**
 * MXS-1828: Multiple LOAD DATA LOCAL INFILE commands in one query cause a hang
 *
 * https://jira.mariadb.org/browse/MXS-1828
 */

#include "testconnections.h"
#include <fstream>

using namespace std;

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);

    const char* query = "LOAD DATA LOCAL INFILE './data.csv' INTO TABLE test.t1";
    const char* filename = "./data.csv";

    unlink(filename);
    ofstream file(filename);

    file << "1\n2\n3" << endl;

    test.set_timeout(30);
    test.maxscales->connect();
    test.try_query(test.maxscales->conn_rwsplit[0], "CREATE TABLE test.t1(id INT)");
    test.try_query(test.maxscales->conn_rwsplit[0], "%s;%s", query, query);

    Row row = get_row(test.maxscales->conn_rwsplit[0], "SELECT COUNT(*) FROM test.t1");
    test.assert(!row.empty() && row[0] == "6", "Both queries should be executed");
    test.try_query(test.maxscales->conn_rwsplit[0], "DROP TABLE test.t1");
    test.maxscales->disconnect();

    unlink(filename);
    return test.global_result;
}
