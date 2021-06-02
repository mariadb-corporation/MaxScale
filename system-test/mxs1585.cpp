/**
 * MXS-1585: https://jira.mariadb.org/browse/MXS-1585
 *
 * Check that MaxScale doesn't crash when the master is set into maintenance
 * mode when master_failure_mode is fail_on_write.
 */

#include <maxtest/testconnections.hh>
#include <vector>

static bool running = true;

void* query_thr(void* data)
{
    TestConnections* test = (TestConnections*)data;

    while (running)
    {
        MYSQL* mysql = test->maxscale->open_rwsplit_connection();

        while (running)
        {
            if (mysql_query(mysql, "INSERT INTO test.mxs1585 VALUES (1)")
                || mysql_query(mysql, "DELETE FROM test.mxs1585 LIMIT 100"))
            {
                break;
            }
        }

        mysql_close(mysql);
    }

    return NULL;
}

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);

    test.maxscale->connect_maxscale();
    test.try_query(test.maxscale->conn_rwsplit[0], "DROP TABLE IF EXISTS test.mxs1585");
    test.try_query(test.maxscale->conn_rwsplit[0], "CREATE TABLE test.mxs1585(id INT) ENGINE=MEMORY");
    test.maxscale->close_maxscale_connections();

    std::vector<pthread_t> threads;
    threads.resize(100);

    for (auto& a : threads)
    {
        pthread_create(&a, NULL, query_thr, &test);
    }

    for (int i = 0; i < 2; i++)
    {
        for (int x = 1; x <= 2; x++)
        {
            test.maxscale->ssh_node_f(0, true, "maxctrl set server server%d maintenance", x);
            sleep(1);
            test.maxscale->ssh_node_f(0, true, "maxctrl clear server server%d maintenance", x);
            sleep(2);
        }
    }

    running = false;
    test.reset_timeout();

    for (auto& a : threads)
    {
        test.reset_timeout();
        pthread_join(a, NULL);
    }

    test.maxscale->connect_maxscale();
    test.try_query(test.maxscale->conn_rwsplit[0], "DROP TABLE test.mxs1585");
    test.check_maxscale_alive();

    return test.global_result;
}
