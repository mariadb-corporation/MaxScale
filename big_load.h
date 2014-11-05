#ifndef BIG_LOAD_H
#define BIG_LOAD_H

#include <my_config.h>
#include "testconnections.h"
#include "sql_t1.h"
#include "get_com_select_insert.h"

//pthread_mutex_t mutex1 = PTHREAD_MUTEX_INITIALIZER;
typedef struct  {
    int exit_flag;
    int i1;
    int i2;
    TestConnections * Test;
} thread_data;

void *query_thread1(void *ptr );
void *query_thread2(void *ptr );

int load(int *new_inserts, int *new_selects, int *selects, int *inserts, int threads_num, TestConnections *Test, int *i1, int *i2);

#endif // BIG_LOAD_H
