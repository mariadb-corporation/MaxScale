/**
 * @file bug601.cpp regression case for bug 601 ("COM_CHANGE_USER fails with correct user/pwd if executed
 * during authentication")
 * - configure Maxscale.cnf to use only one thread
 * - in 100 parallel threads start to open/close session
 * - do change_user 2000 times
 * - check all change_user are ok
 * - check Mascale is alive
 */

#include <maxtest/testconnections.hh>
#include <atomic>
#include <iostream>

using namespace std;

std::atomic_int exit_flag {0};
TestConnections* Test {nullptr};

void* parall_traffic(void* ptr);

int main(int argc, char* argv[])
{
    int iterations = 1000;
    Test = new TestConnections(argc, argv);
    if (Test->smoke)
    {
        iterations = 100;
    }

    std::thread parall_traffic1[100];

    Test->repl->connect();
    Test->repl->execute_query_all_nodes((char*) "set global max_connect_errors=1000;");
    Test->repl->execute_query_all_nodes((char*) "set global max_connections=1000;");

    Test->maxscale->connect_maxscale();
    Test->tprintf("Creating one user 'user@%%'");
    execute_query_silent(Test->maxscale->conn_rwsplit[0], (char*) "DROP USER user@'%'");
    Test->try_query(Test->maxscale->conn_rwsplit[0], (char*) "CREATE USER user@'%%' identified by 'pass2'");
    Test->try_query(Test->maxscale->conn_rwsplit[0], (char*) "GRANT SELECT ON test.* TO user@'%%';");
    Test->try_query(Test->maxscale->conn_rwsplit[0], (char*) "FLUSH PRIVILEGES;");

    Test->tprintf("Starting parallel thread which opens/closes session in the loop");

    for (int j = 0; j < 25; j++)
    {
        parall_traffic1[j] = std::thread(parall_traffic, nullptr);
    }

    Test->tprintf("Doing change_user in the loop");
    for (int i = 0; i < iterations; i++)
    {
        Test->add_result(mysql_change_user(Test->maxscale->conn_rwsplit[0], "user", "pass2", (char*) "test"),
                         "change_user failed! %s", mysql_error(Test->maxscale->conn_rwsplit[0]));
        Test->add_result(mysql_change_user(Test->maxscale->conn_rwsplit[0],
                                           Test->maxscale->user_name.c_str(),
                                           Test->maxscale->password.c_str(),
                                           (char*) "test"), "change_user failed! %s",
                         mysql_error(Test->maxscale->conn_rwsplit[0]));
    }

    Test->tprintf("Waiting for all threads to finish");
    exit_flag = 1;
    for (int j = 0; j < 25; j++)
    {
        parall_traffic1[j].join();
    }
    Test->tprintf("All threads are finished");

    Test->tprintf("Change user to '%s' in order to be able to DROP user",
                  Test->maxscale->user_name.c_str());
    mysql_change_user(Test->maxscale->conn_rwsplit[0],
                      Test->maxscale->user_name.c_str(),
                      Test->maxscale->password.c_str(),
                      NULL);

    Test->tprintf("Dropping user");
    Test->try_query(Test->maxscale->conn_rwsplit[0], (char*) "DROP USER user@'%%';");

    Test->set_verbose(true);
    Test->check_maxscale_alive();
    Test->set_verbose(false);

    int rval = Test->global_result;
    delete Test;
    return rval;
}

void* parall_traffic(void* ptr)
{
    while (exit_flag == 0)
    {
        MYSQL* conn = Test->maxscale->open_rwsplit_connection();

        while (exit_flag == 0 && mysql_query(conn, "DO 1") == 0)
        {
            sleep(1);
        }

        mysql_close(conn);
    }

    return NULL;
}
