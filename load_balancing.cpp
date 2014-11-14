#include <my_config.h>
#include "testconnections.h"
#include "sql_t1.h"
#include "get_com_select_insert.h"

#include "big_load.h"

int main()
{

    TestConnections * Test = new TestConnections();
    int global_result = 0;
    int q;

    int selects[256];
    int inserts[256];
    int new_selects[256];
    int new_inserts[256];
    int i1, i2;

    Test->ReadEnv();
    Test->PrintIP();

    global_result += load(&new_inserts[0], &new_selects[0], &selects[0], &inserts[0], 25, Test, &i1, &i2, 1);

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

    if ((new_selects[0] - selects[0]) > 100 ) {
        printf("FAILED: number of queries for master greater then 100\n");
        global_result++;
    }

    global_result += CheckMaxscaleAlive();
    exit(global_result);
}
