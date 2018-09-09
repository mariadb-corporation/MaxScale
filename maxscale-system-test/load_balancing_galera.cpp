/**
 * @file load_balancing_galera.cpp Checks how Maxscale balances load
 *
 * - also used for 'load_balancing_galera_pers1' and 'load_balancing_galera_pers10' tests (with
 *'persistpoolmax=1' and 'persistpoolmax=10' for all servers)
 *
 * - start two groups of threads: each group consists of 25 threads, each thread creates connections to
 *RWSplit,
 * threads from first group try to execute as many SELECTs as possible, from second group - one query per
 *second
 * - after 100 seconds all threads are stopped
 * - check number of connections to every slave: test PASSED if COM_SELECT difference between slaves is not
 *greater then 3 times and no
 * more then 10% of quesries went to Master
 */



#include "testconnections.h"
#include "sql_t1.h"
#include "get_com_select_insert.h"

#include "big_load.h"

int main(int argc, char* argv[])
{

    TestConnections* Test = new TestConnections(argc, argv);
    int master;
    long int q;
    int threads_num = 25;

    long int selects[256];
    long int inserts[256];
    long int new_selects[256];
    long int new_inserts[256];
    long int i1, i2;

    Test->set_timeout(20);
    master = Test->maxscales->find_master_maxadmin(Test->galera);

    if (master >= 0)
    {
        Test->tprintf("Master node is %d (server%d)\n", master, master + 1);
        Test->set_timeout(20);

        if (Test->smoke)
        {
            threads_num = 15;
        }
        Test->galera->connect();
        for (int i = 0; i < Test->galera->N; i++)
        {
            execute_query(Test->galera->nodes[i], (char*) "set global max_connections = 300;");
            execute_query(Test->galera->nodes[i], (char*) "set global max_connect_errors = 100000;");
        }
        Test->galera->close_connections();

        Test->set_timeout(1200);
        load(&new_inserts[0],
             &new_selects[0],
             &selects[0],
             &inserts[0],
             threads_num,
             Test,
             &i1,
             &i2,
             1,
             true,
             true);

        long int avr = (i1 + i2 ) / (Test->galera->N);
        Test->tprintf("average number of quries per node %ld\n", avr);
        long int min_q = avr / 3;
        long int max_q = avr * 3;
        Test->tprintf("Acceplable value for every node from %ld until %ld\n", min_q, max_q);

        for (int i = 0; i < Test->galera->N; i++)
        {
            if (i != master)
            {
                q = new_selects[i] - selects[i];
                if ((q > max_q) || (q < min_q))
                {
                    Test->add_result(1, "number of queries for node %d is %ld\n", i + 1, q);
                }
            }
        }

        if ((new_selects[master] - selects[master]) > avr / 3)
        {
            Test->add_result(1,
                             "number of queries for master greater then 30%% of averange number of queries per node\n");
        }

        Test->tprintf("Restoring nodes\n");
        Test->galera->connect();
        for (int i = 0; i < Test->galera->N; i++)
        {
            execute_query(Test->galera->nodes[i], (char*) "flush hosts;");
            execute_query(Test->galera->nodes[i], (char*) "set global max_connections = 151;");
        }
        Test->galera->close_connections();

        Test->check_maxscale_alive(0);
    }
    else
    {
        Test->add_result(1, "Master is not found\n");
    }

    int rval = Test->global_result;
    delete Test;
    return rval;
}
