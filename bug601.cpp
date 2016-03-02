/**
 * @file bug601.cpp regression case for bug 601 ("COM_CHANGE_USER fails with correct user/pwd if executed during authentication")
 * - configure Maxscale.cnf to use only one thread
 * - in 100 parallel threads start to open/close session
 * - do change_user 2000 times
 * - check all change_user are ok
 * - check Mascale is alive
 */

#include <my_config.h>
#include <iostream>
#include "testconnections.h"

using namespace std;

pthread_mutex_t mutex1 = PTHREAD_MUTEX_INITIALIZER;
int exit_flag = 0;

TestConnections * Test ;

void *parall_traffic( void *ptr );


int main(int argc, char *argv[])
{
    int iterations = 1000;
    Test = new TestConnections(argc, argv);
    if (Test->smoke) {iterations = 100;}
    Test->set_timeout(10);

    pthread_t parall_traffic1[100];
    int check_iret[100];

    Test->read_env();
    Test->print_env();
    Test->repl->connect();
    fflush(stdout);


    Test->add_result(Test->connect_rwsplit(), "Error connecting to RWSplit\n");

    Test->repl->connect();
    for (int k = 0; k < Test->repl->N; k++) {
        execute_query(Test->repl->nodes[k], (char *) "set global max_connect_errors=1000;");
    }
    Test->repl->close_connections();

    Test->tprintf("Creating one more user\n");
    Test->try_query(Test->conn_rwsplit, (char *) "GRANT SELECT ON test.* TO user@'%'  identified by 'pass2';");
    Test->try_query(Test->conn_rwsplit, (char *) "FLUSH PRIVILEGES;");

    Test->tprintf("Starting parallel thread which opens/closes session in the loop\n");

    if (Test->conn_rwsplit != NULL) {

        for (int j = 0; j < 25; j++) {
            check_iret[j] = pthread_create( &parall_traffic1[j], NULL, parall_traffic, NULL);
        }

        printf("Doing change_user in the loop\n");fflush(stdout);
        for (int i = 0; i < iterations; i++) {
            Test->set_timeout(15);
            Test->add_result(mysql_change_user(Test->conn_rwsplit, "user", "pass2", (char *) "test"), "change_user failed!\n");
            Test->add_result(mysql_change_user(Test->conn_rwsplit, Test->maxscale_user, Test->maxscale_password, (char *) "test"), "change_user failed!\n"); fflush(stdout);
            if ((i / 100) * 100 == i) {Test->tprintf("i=%d\n", i);}
        }

        Test->set_timeout(30);
        exit_flag = 1;
        for (int j = 0; j < 25; j++) {
            pthread_join(check_iret[j], NULL);
        }
        sleep(3);
        Test->set_timeout(10);
        mysql_change_user(Test->conn_rwsplit, Test->maxscale_user, Test->maxscale_password, NULL);

        Test->try_query(Test->conn_rwsplit, (char *) "DROP USER user@'%';");
        Test->close_rwsplit();
        Test->check_maxscale_alive();
    } else {
        Test->add_result(Test->connect_rwsplit(), "Error connecting to RWSplit\n");
    }

    Test->copy_all_logs(); return(Test->global_result);
}

void *parall_traffic( void *ptr )
{
    MYSQL * conn;
    while (exit_flag == 0) {
        conn = Test->open_rwsplit_connection();
        mysql_close(conn);
    }
    return NULL;
}

