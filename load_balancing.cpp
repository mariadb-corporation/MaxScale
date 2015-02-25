/**
 * @file load_balancing.cpp Checks how Maxscale balances load
 *
 * - start two groups of threads: each group consists of 25 threads, each thread creates connections to RWSplit,
 * threads from first group try to execute as many SELECTs as possible, from second group - one query per second
 * - after 100 seconds all threads are stopped
 * - check number of connections to every slave: test PASSED if COM_SELECT difference between slaves is not greater then 3 times and no
 * more then 10% of quesries went to Master
 */


#include <my_config.h>
#include "testconnections.h"
#include "sql_t1.h"
#include "get_com_select_insert.h"

#include "big_load.h"

int main(int argc, char *argv[])
{

    TestConnections * Test = new TestConnections(argc, argv);
    int global_result = 0;
    int q;

    int selects[256];
    int inserts[256];
    int new_selects[256];
    int new_inserts[256];
    int i1, i2;

    Test->ReadEnv();
    Test->PrintIP();

    Test->repl->Connect();
    for (int i = 0; i < Test->repl->N; i++) {
        execute_query(Test->repl->nodes[i], (char *) "set global max_connections = 300;");
        execute_query(Test->repl->nodes[i], (char *) "set global max_connect_errors = 100000;");
    }
    Test->repl->CloseConn();

    global_result += load(&new_inserts[0], &new_selects[0], &selects[0], &inserts[0], 25, Test, &i1, &i2, 1, FALSE);

    int avr = (i1 + i2 ) / (Test->repl->N);
    printf("average number of quries per node %d\n", avr);
    int min_q = avr / 3;
    int max_q = avr * 3;
    printf("Acceplable value for every node from %d until %d\n", min_q, max_q);

    for (int i = 1; i < Test->repl->N; i++) {
        q = new_selects[i] - selects[i];
        if ((q > max_q) || (q < min_q)) {
            printf("FAILED: number of queries for node %d is %d\n", i+1, q);
            global_result++;
        }
    }

    if ((new_selects[0] - selects[0]) > avr / 3 ) {
        printf("FAILED: number of queries for master greater then 30%% of averange number of queries per node\n");
        global_result++;
    }

    printf("Restoring nodes\n"); fflush(stdout);
    Test->repl->Connect();
    for (int i = 0; i < Test->repl->N; i++) {
        execute_query(Test->repl->nodes[i], (char *) "flush hosts;");
        execute_query(Test->repl->nodes[i], (char *) "set global max_connections = 151;");
    }
    Test->repl->CloseConn();


    global_result += CheckMaxscaleAlive();

    Test->repl->StartReplication();

    Test->Copy_all_logs(); return(global_result);
}
