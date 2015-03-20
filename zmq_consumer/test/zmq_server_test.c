/*
 * This file is distributed as part of MaxScale by MariaDB Corporation.  It is free
 * software: you can redistribute it and/or modify it under the terms of the
 * GNU General Public License as published by the Free Software Foundation,
 * version 2.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * The ZMQ consumer is part of MaxWeb a monitoring and administration tool for MaxScale. 
 * Visit our site http://www.maxweb.io for more information
 * 
 * Copyright Proxylab 2014 Thessaloniki-Greece http://www.proxylab.io
 * 
 */

/**
 * @file zmq_server_test.c
 * 
 * Use this tool to stimulate traffic to maxscale. The program generates and sends requests to maxscale
 * based on a specific query list (see below).
 * 
 */

#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <mysql.h>
#include "../../rabbitmq_consumer/inih/ini.h"
#include <time.h>

#define RAND_QUERIES 15
char *queries[RAND_QUERIES] = {
    "select dept_no from departments where dept_no = 1",//the 1st query is used to check throughput and should be very fast!!!
    "select emp_no, birth_date, last_name from employees where emp_no = 500;",
    "select * from employees where emp_no = 6596;",
    "select count(*) from employees where (emp_no > 0 and emp_no < 3236) and gender = 'F';",
    "select e.emp_no, first_name, last_name, salary from employees e INNER JOIN salaries s ON e.emp_no = s.emp_no where e.emp_no = 56465;",
    "select * from departments order by dept_no asc",
    "select * from departments order by dept_no_ asc",//error 1
    "update departments set dept_name = 'zzz dept' where dept_no = 1",
    "update departments set dept_name = 'zzz dept' where dept_no = 3",
    "update departments_ set dept_name = 'zzz dept' where dept_no = 3",//error 2
    "update departments set dept_name = 'agrafiotis' where dept_no = 2",
    "update departments set dept_name = 'agrafiotis dept' where dept_no = 5",
    "delete from departments where dept_no = 565699",
    "delete from departments where dept_no = 5656995",
    "delete from departments where dept_no_ = 5656995" //error 3
};

typedef struct
{
    char        *db_server;
    char        *db_uname;
    char        *db_passwd;
    int         db_port;
    int         records;
    int         threads;
    bool        delay_enabled;
    int         delay_from;
    int         delay_to;
    bool        test_throughput_only;
} config;

static int configHandler(void* cnfg, const char* section, const char* name, const char* value);

static config *cnfg;
pthread_mutex_t lock;

int range_rand(int min, int max)
{
    int diff = max - min;
    return (int) (((double)(diff+1)/RAND_MAX) * rand() + min);
}

static void* worker_routine (void *args) {
    
    MYSQL mysql;  
    mysql_init(&mysql);
    mysql_options(&mysql, MYSQL_READ_DEFAULT_GROUP, "zmq_server_test");
    my_bool reconnect = 1;
    mysql_options(&mysql, MYSQL_OPT_RECONNECT, &reconnect);
    
    if (!mysql_real_connect(&mysql, cnfg->db_server, cnfg->db_uname, cnfg->db_passwd, "maxtest", cnfg->db_port, NULL, 0))
    {
        printf("Failed to connect to database: Error: %s\n", mysql_error(&mysql));
        mysql_close(&mysql);
        mysql_thread_end();
        return NULL;
    }
    
    int executed_queries = 0;
    int i =0;
    for(i = 0; i<cnfg->records; i++){
        int q_rand_indx = cnfg->test_throughput_only ? 0 : range_rand(0, RAND_QUERIES - 1);
        mysql_query(&mysql, queries[q_rand_indx]);
        
        if(q_rand_indx < RAND_QUERIES - 8){ //call mysql_store_result only for select queries!!!
             MYSQL_RES *result = mysql_store_result(&mysql);

             if (result != NULL){
                int num_fields = mysql_num_fields(result);

                MYSQL_ROW row;

                while ((row = mysql_fetch_row(result)));

                mysql_free_result(result);
             }
        }
 
        executed_queries++;
        if(executed_queries % 1000 == 0){
            printf ("Executed requests:%d\n", executed_queries); 
        }
                
        if(cnfg->delay_enabled){
            usleep(range_rand(cnfg->delay_from, cnfg->delay_to));
        }
    }
    
    mysql_close(&mysql);
    mysql_thread_end();
    
    return NULL;
}

int main (int argc, char *argv [])
{
    srand(time(NULL));
    cnfg = (config *)malloc(sizeof(config));
    
     if(cnfg->delay_enabled)
            fprintf(stderr, "I: Delaying is enabled\n");
    
    if (mysql_library_init(0, NULL, NULL)) {
        fprintf(stderr, "could not initialize MySQL library\n");
        return 1;
      }
    
    if (ini_parse("config_test.ini", configHandler, cnfg) < 0) {
        printf("Can't load 'config_test.ini'\n");
        return 1;
    }
    
    if (pthread_mutex_init(&lock, NULL) != 0)
    {
        printf("\n mutex init failed\n");
        return 1;
    }

    int thread_nmr = 0;
    pthread_t threads[cnfg->threads];
    while (thread_nmr++ < cnfg->threads)
        pthread_create (&threads[thread_nmr], NULL, worker_routine, NULL);

    thread_nmr = 0;
    while (thread_nmr++ < cnfg->threads)
        pthread_join(threads[thread_nmr], NULL);
    
    pthread_mutex_destroy(&lock);
    mysql_library_end();
    
    return 0;
}

static int configHandler(void* cnfg, const char* section, const char* name, const char* value)
{
    config* pconfig = (config*)cnfg;

    #define MATCH(s, n) strcmp(section, s) == 0 && strcmp(name, n) == 0

    if (MATCH("global", "dbserver")) {
        pconfig->db_server = strdup(value);
    } else if (MATCH("global", "dbport")) {
        pconfig->db_port = atoi(value);
    } else if (MATCH("global", "dbuser")) {
        pconfig->db_uname = strdup(value);
    } else if (MATCH("global", "dbpasswd")) {
        pconfig->db_passwd = strdup(value);
    } else if (MATCH("global", "records")) {
        pconfig->records = atoi(value);
    } else if (MATCH("global", "threads")) {
        pconfig->threads = atoi(value);
    } else if (MATCH("global", "delay_from")) {
        pconfig->delay_from = atoi(value);
    } else if (MATCH("global", "delay_to")) {
        pconfig->delay_to = atoi(value);
    } else if (MATCH("global", "delay_enabled")) {
        pconfig->delay_enabled = (strcasecmp(value, "true") == 0 ? true : false);
    } else if (MATCH("global", "test_throughput_only")) {
        pconfig->test_throughput_only = (strcasecmp(value, "true") == 0 ? true : false);
    }    
    else {
        return 0;  /* unknown section/name, error */
    }
    return 1;
}
