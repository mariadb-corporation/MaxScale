#include <my_config.h>
#include <iostream>
#include <unistd.h>
#include "testconnections.h"

#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <netdb.h>
#include <string.h>
#include <openssl/sha.h>
#include "maxinfo_func.h"
#include "sql_t1.h"
#include <sys/epoll.h>

using namespace std;
char *reg_str= (char *) "REGISTER UUID=XXX-YYY_YYY, TYPE=JSON";
char *req_str= (char *) "REQUEST-DATA test.t1";
int insert_val = 0;
bool exit_flag = false;

void *query_thread( void *ptr );

char * cdc_com(TestConnections * Test)
{
    struct sockaddr_in *remote;
    char buf[BUFSIZ+1];
    int tmpres;
    int sock;
    char *ip;
    char *get;

    int max_inserted_val = Test->smoke ? 25 : 100;

    sock = create_tcp_socket();
    Test->tprintf("host %s\n", Test->maxscale_IP);
    ip = get_ip(Test->maxscale_IP);
    if (ip == NULL)
    {
        Test->add_result(1, "Can't get IP\n");
        return NULL;
    }
    Test->tprintf("IP=%s\n", ip);
    remote = (struct sockaddr_in *)malloc(sizeof(struct sockaddr_in *));
    remote->sin_family = AF_INET;
    tmpres = inet_pton(AF_INET, ip, (void *)(&(remote->sin_addr.s_addr)));
    if( tmpres < 0)
    {
        Test->add_result(1, "Can't set remote->sin_addr.s_addr\n");
        return NULL;
    } else if (tmpres == 0)
    {
        Test->add_result(1, "%s is not a valid IP address\n", ip);
        return NULL;
    }
    remote->sin_port = htons(4001);

    if(connect(sock, (struct sockaddr *)remote, sizeof(struct sockaddr)) < 0){
        Test->add_result(1, "Could not connect\n");
        return NULL;
    }

    get = cdc_auth_srt((char *) "skysql", (char *) "skysql");
    //get = (char *) "736b7973716c3a454ac34c2999aacfebc6bf5fe9fa1db9b596f625";
    Test->tprintf("get: %s\n", get);
    //Send the query to the server
    if (send_so(sock, get) != 0)
    {
        Test->add_result(1, "Cat't send data to scoket\n");
        return NULL;
    }
    //free(get);
    char buf1[1024];
    recv(sock, buf1, 1024, 0);
    Test->tprintf("%s\n", buf);

    Test->tprintf("reg: %s\n", reg_str);
    //Send the query to the server
    if (send_so(sock, reg_str) != 0)
    {
        Test->add_result(1, "Cat't send data to scoket\n");
        return NULL;
    }
    recv(sock, buf1, 1024, 0);
    Test->tprintf("%s\n", buf);
    Test->tprintf("req: %s\n", req_str);
    //Send the query to the server
    if (send_so(sock, req_str) != 0)
    {
        Test->add_result(1, "Cat't send data to scoket\n");
        return NULL;
    }

    Test->stop_timeout();
    //*****
    int epfd = epoll_create(1);
    static struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLPRI | EPOLLERR | EPOLLHUP;
    ev.data.fd = sock;
    Test->tprintf("epoll_ctl\n");
    int eres = epoll_ctl(epfd, EPOLL_CTL_ADD, sock, &ev);
    if (eres < 0)
    {
        Test->tprintf("Error in epoll_ctl! errno = %d, %s\n", errno, strerror(errno));
        return NULL;
    }

    epoll_event events[2];
    int fd;
    setnonblocking(sock);
    char *json;
    long long int x1;
    long long int fl;
    int inserted_val = 0;
    int ignore_first = 2;

    while (inserted_val < max_inserted_val) {
        // wait for something to do...
        Test->tprintf("epoll_wait\n");
        int nfds = epoll_wait(epfd, &events[0], 1, -1);
        if (nfds < 0) {
            Test->tprintf("Error in epoll_wait! errno = %d, %s\n", errno, strerror(errno));
            return NULL;
        }

        if (nfds > 0)
        {
            // for each ready socket
            //for(int i = 0; i < nfds; i++)
            //{
            fd = events[0].data.fd;
            json = read_sc(sock);
            Test->tprintf("%s\n", json);
            //}
            if (ignore_first > 0)
            {
                ignore_first--; // ignoring first reads
                if (ignore_first == 0)
                {
                    // first reads done, starting inserting
                    insert_val = 10;
                    inserted_val = insert_val;
                }
            } else {
                // trying to check JSON
                get_x_fl_from_json(json, &x1, &fl);
                Test->tprintf("data received, x1=%lld fl=%lld\n", x1, fl);
                if ((x1 != inserted_val) || (fl != inserted_val + 100))
                {
                    Test->add_result(1, "wrong values in JSON\n");
                }
                inserted_val++;
                insert_val = inserted_val;
            }
            free(json);
        } else {
            Test->tprintf("waiting\n");
        }
    }

    free(remote);
    free(ip);
    close(sock);

    //return result;
    return NULL;
}

TestConnections * Test;
int main(int argc, char *argv[])
{
    Test = new TestConnections(argc, argv);

    Test->set_timeout(600);
    Test->stop_maxscale();
    Test->ssh_maxscale(TRUE, (char *) "rm -rf /var/lib/maxscale/avro");

    Test->repl->connect();
    execute_query(Test->repl->nodes[0], (char *) "DROP TABLE IF EXISTS t1;");
    Test->repl->close_connections();
    sleep(5);

    Test->binlog_cmd_option = 0;
    Test->start_binlog();

    Test->set_timeout(120);
    Test->stop_maxscale();
    Test->ssh_maxscale(TRUE, (char *) "rm -rf /var/lib/maxscale/avro");
    Test->set_timeout(120);
    Test->start_maxscale();
    Test->set_timeout(60);

    Test->repl->connect();
    create_t1(Test->repl->nodes[0]);
    execute_query(Test->repl->nodes[0], (char *) "INSERT INTO t1 VALUES (111, 222)");
    //insert_into_t1(Test->repl->nodes[0], 3);
    Test->repl->close_connections();
    sleep(10);

    Test->set_timeout(120);

    pthread_t thread;
    pthread_create( &thread, NULL, query_thread, NULL);

    char * result = cdc_com(Test);

    exit_flag = true;

    pthread_join(thread, NULL);

    Test->tprintf("%s\n", result);

    //Test->check_maxscale_alive();

    Test->copy_all_logs();
    return(Test->global_result);
}


void *query_thread( void *ptr )
{
    char str[256];

    Test->repl->connect();
    while (!exit_flag)
    {
        if (insert_val != 0)
        {
            sprintf(str, "INSERT INTO t1 VALUES (%d, %d)", insert_val, insert_val + 100);
            insert_val = 0;
            printf("%s\n", str);
            execute_query(Test->repl->nodes[0], str);
        }
        //sleep(1);
    }
    Test->repl->close_connections();
}
