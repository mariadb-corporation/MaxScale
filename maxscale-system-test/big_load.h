#ifndef BIG_LOAD_H
#define BIG_LOAD_H


#include "testconnections.h"
#include "sql_t1.h"
#include "get_com_select_insert.h"

// pthread_mutex_t mutex1 = PTHREAD_MUTEX_INITIALIZER;
typedef struct
{
    int              exit_flag;
    long             i1;
    long             i2;
    int              rwsplit_only;
    TestConnections* Test;
} thread_data;

void* query_thread1(void* ptr);
void* query_thread2(void* ptr);

/**
 * @brief load Creates load on Maxscale routers
 * @param new_inserts COM_INSERT variable values array for all nodes after test
 * @param new_selects COM_SELECT variable values array for all nodes after test
 * @param selects COM_SELECT variable values array for all nodes before test
 * @param inserts COM_INSERT variable values array for all nodes before test
 * @param threads_num Number of load threads
 * @param Test TestConnections object
 * @param i1 Number of queries executed by "fast" threads (no wating between queries)
 * @param i2 Number of queries executed by "slow" threads (sleep 1 second between queries)
 * @param rwsplit_only if 1 create load only on RWSplit router, do not load ReadConn router
 * @param galera if true use Galera backend (Test->galera instead of Test->repl)
 * @param report_errors if true call add_result() in case of query failure
 */
void load(long* new_inserts,
          long* new_selects,
          long* selects,
          long* inserts,
          int   threads_num,
          TestConnections* Test,
          long* i1,
          long* i2,
          int   rwsplit_only,
          bool  galera,
          bool  report_errors);

#endif      // BIG_LOAD_H
