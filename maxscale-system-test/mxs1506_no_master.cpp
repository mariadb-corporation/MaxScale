/**
 * MXS-1506: Delayed query retry without master
 *
 * https://jira.mariadb.org/browse/MXS-1506
 */
#include "testconnections.h"
#include <string>
#include <functional>
#include <thread>
#include <iostream>
#include <vector>

using namespace std;

bool query(TestConnections& test)
{
    test.maxscales->connect();
    execute_query_silent(test.maxscales->conn_rwsplit[0], "SET @a = 1") == 0;
    sleep(5);
    auto row = get_row(test.maxscales->conn_rwsplit[0], "SELECT @a");
    test.maxscales->disconnect();
    return row[0] == "1";
}

void block(TestConnections& test, std::vector<int> nodes)
{
    for (auto a : nodes)
    {
        test.repl->block_node(a);
    }

    sleep(10);

    for (auto a : nodes)
    {
        test.repl->unblock_node(a);
    }
}

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);
    thread thr;

    cout << "Blocking the master and executing a SELECT" << endl;
    thr = thread(block, std::ref(test), vector<int>({0}));
    test.assert(query(test), "Select without master should work");
    thr.join();

    cout << "Blocking the slave and executing a SELECT" << endl;
    thr = thread(block, std::ref(test), vector<int>({1}));
    test.assert(query(test), "Select without slave should work");
    thr.join();

    cout << "Blocking both servers and executing a SELECT" << endl;
    thr = thread(block, std::ref(test), vector<int>({0, 1}));
    test.assert(query(test), "Select with no servers should work");
    thr.join();

    return test.global_result;
}
