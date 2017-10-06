/**
 * @file bug601.cpp regression case for bug 601 ("COM_CHANGE_USER fails with correct user/pwd if executed during authentication")
 * - configure Maxscale.cnf to use only one thread
 * - in 100 parallel threads start to open/close session
 * - do change_user 2000 times
 * - check all change_user are ok
 * - check Mascale is alive
 */

/*
Vilho Raatikka 2014-10-30 14:30:57 UTC
If COM_CHANGE_USER is executed while backend protocol's state is not yet MYSQL_AUTH_RECV it will fail in the backend.

If MaxScale uses multiple worked threads this occurs rarely and it would be possible to easily suspend execution of COM_CHANGE_USER.

If MaxScale uses one worker thread then there's currently no way to suspend execution. It would require thread to put current task on hold, complete authentication task and return to COM_CHANGE_USER execution.

In theory it is possible to add an event to client's DCB and let it become notified in the same way than events that occur in sockets. It would have to be added first (not last) and ensure that no other command is executed before it.

Since this is the only case known where this would be necessary, it could be enough to add a "pending auth change" pointer in client's protocol object which would be checked before thread returns to epoll_wait after completing the authentication.
Comment 1 Massimiliano 2014-11-07 17:01:29 UTC
Current code in develop branch let COM_CHANGE_USER work fine.

I noticed sometime a failed authentication issue using only.
This because backend protocol's state is not yet MYSQL_AUTH_RECV and necessary data for succesfull backend change user (such as scramble data from handshake) may be not available.


I put a query before change_user and the issue doesn't appear: that's another proof.
Comment 2 Vilho Raatikka 2014-11-13 15:54:15 UTC
In gw_change_user->gw_send_change_user_to_backend authentication message was sent to backend server before backend had its scramble data. That caused authentication to fail.
Comment 3 Vilho Raatikka 2014-11-13 15:58:34 UTC
if (func.auth ==)gw_change_user->gw_send_change_user_to_backend is called before backend has its scramble, auth packet is set to backend's delauqueue instead of writing it to backend. When backend_write_delayqueue is called COM_CHANGE_USER packets are rewritten with backend's current data.
*/


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
    if (Test->smoke)
    {
        iterations = 100;
    }


    pthread_t parall_traffic1[100];
    int check_iret[100];

    Test->set_timeout(60);
    Test->repl->connect();
    Test->repl->execute_query_all_nodes((char *) "set global max_connect_errors=1000;");
    Test->repl->execute_query_all_nodes((char *) "set global max_connections=1000;");

    Test->maxscales->connect_maxscale(0);
    Test->tprintf("Creating one user 'user@%%'");
    execute_query_silent(Test->maxscales->conn_rwsplit[0], (char *) "DROP USER user@'%'");
    Test->try_query(Test->maxscales->conn_rwsplit[0], (char *) "CREATE USER user@'%%' identified by 'pass2'");
    Test->try_query(Test->maxscales->conn_rwsplit[0], (char *) "GRANT SELECT ON test.* TO user@'%%';");
    Test->try_query(Test->maxscales->conn_rwsplit[0], (char *) "FLUSH PRIVILEGES;");

    Test->tprintf("Starting parallel thread which opens/closes session in the loop");

    for (int j = 0; j < 25; j++)
    {
        check_iret[j] = pthread_create(&parall_traffic1[j], NULL, parall_traffic, NULL);
    }

    Test->tprintf("Doing change_user in the loop");
    for (int i = 0; i < iterations; i++)
    {
        Test->set_timeout(15);
        Test->add_result(mysql_change_user(Test->maxscales->conn_rwsplit[0], "user", "pass2", (char *) "test"),
                         "change_user failed! %", mysql_error(Test->maxscales->conn_rwsplit[0]));
        Test->add_result(mysql_change_user(Test->maxscales->conn_rwsplit[0], Test->maxscales->user_name, Test->maxscales->password,
                                           (char *) "test"), "change_user failed! %s", mysql_error(Test->maxscales->conn_rwsplit[0]));
    }

    Test->tprintf("Waiting for all threads to finish");
    exit_flag = 1;
    for (int j = 0; j < 25; j++)
    {
        Test->set_timeout(30);
        pthread_join(parall_traffic1[j], NULL);
    }
    Test->tprintf("All threads are finished");
    Test->repl->flush_hosts();

    Test->tprintf("Change user to '%s' in order to be able to DROP user", Test->maxscales->user_name);
    Test->set_timeout(30);
    mysql_change_user(Test->maxscales->conn_rwsplit[0], Test->maxscales->user_name, Test->maxscales->password, NULL);

    Test->tprintf("Dropping user", Test->maxscales->user_name);
    Test->try_query(Test->maxscales->conn_rwsplit[0], (char *) "DROP USER user@'%%';");
    Test->check_maxscale_alive(0);

    int rval = Test->global_result;
    delete Test;
    return rval;
}

void *parall_traffic( void *ptr )
{
    MYSQL * conn;
    while (exit_flag == 0)
    {
        conn = Test->maxscales->open_rwsplit_connection(0);
        mysql_close(conn);
        if (Test->backend_ssl)
        {
            sleep(1);
        }
    }
    return NULL;
}

