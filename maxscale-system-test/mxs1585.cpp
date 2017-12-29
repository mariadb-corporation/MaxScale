/**
 * MXS-1585: https://jira.mariadb.org/browse/MXS-1585
 *
 * Check that MaxScale doesn't crash when the master is set into maintenance
 * mode when master_failure_mode is fail_on_write.
 */

#include "testconnections.h"
#include <vector>

static bool running = true;

void* query_thr(void* data)
{
    TestConnections* test = (TestConnections*)data;

    while (running)
    {
        MYSQL* mysql = test->open_rwsplit_connection();

        while (running)
        {
            if (mysql_query(mysql, "SET sql_log_bin = 0") ||
                mysql_query(mysql, "INSERT INTO test.mxs1585 VALUES (1)") ||
                mysql_query(mysql, "DELETE FROM test.mxs1585"))
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

    test.connect_maxscale();
    test.try_query(test.conn_rwsplit, "DROP TABLE IF EXISTS test.mxs1585");
    test.try_query(test.conn_rwsplit, "CREATE TABLE test.mxs1585(id INT) ENGINE=MEMORY");
    test.close_maxscale_connections();

    std::vector<pthread_t> threads;
    threads.resize(100);

    for (auto& a: threads)
    {
        pthread_create(&a, NULL, query_thr, &test);
    }

    for (int i = 0; i < 15; i++)
    {
        for (int x = 1; x <= 4; x++)
        {
            test.ssh_maxscale(true, "maxadmin set server server%d maintenance", x);
            sleep(1);
            test.ssh_maxscale(true, "maxadmin clear server server%d maintenance", x);
            sleep(2);

            test.ssh_maxscale(true, "maxadmin remove server server%d \"RW Split Router\" \"Galera Monitor\"", x);
            sleep(1);
            test.ssh_maxscale(true, "maxadmin add server server%d \"RW Split Router\" \"Galera Monitor\"", x);
            sleep(2);

            test.galera->block_node(x - 1);
            sleep(5);
            test.galera->unblock_node(x - 1);
            sleep(5);
        }
    }

    running = false;

    for (auto& a: threads)
    {
        pthread_join(a, NULL);
    }

    test.connect_maxscale();
    test.try_query(test.conn_rwsplit, "DROP TABLE test.mxs1585");
    test.check_maxscale_alive();

    return test.global_result;
}
