#ifndef BIG_LOAD_H
#define BIG_LOAD_H

#include <my_config.h>
#include "testconnections.h"
#include "sql_t1.h"
#include "get_com_select_insert.h"

//pthread_mutex_t mutex1 = PTHREAD_MUTEX_INITIALIZER;
typedef struct  {
    int exit_flag;
    long i1;
    long i2;
    int rwsplit_only;
    TestConnections * Test;
} thread_data;

void *query_thread1(void *ptr );
void *query_thread2(void *ptr );

void load(long *new_inserts, long *new_selects, long *selects, long *inserts, int threads_num, TestConnections *Test, long *i1, long *i2, int rwsplit_only, bool galera, bool report_errors);

#endif // BIG_LOAD_H
