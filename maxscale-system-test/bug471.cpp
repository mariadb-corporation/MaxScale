/**
 * @file bug471.cpp bug471 regression case ( Routing Hints route to server sometimes doesn't work )
 *
 * - try "select @@server_id; -- maxscale route to server server%d" (where %d - server number) and compares
 * result
 * with "select @@server_id;" sent directly to backend node.
 * - do it 25 times.
 */

/*
 *  Massimiliano 2014-08-06 13:27:05 UTC
 *  I found using basic routing hints such as:
 *
 *  select @@server_id; -- maxscale route to server server4
 *  select @@server_id; -- maxscale route to server server3
 *  select @@server_id; -- maxscale route to server server2
 *
 *  server3 is the current master
 *
 *  and server4 server_id is 4
 *  server3 server_id is 3
 *  server2 server_id is 2
 *
 *  sometimes I cannot get the expected results that are:
 *
 *  4
 *  3
 *  2
 *
 *
 *  Sometimes I got:
 *
 *  2
 *  3
 *  2
 *
 *  or
 *
 *  4
 *  3
 *  4
 *
 *  In maxScale configuration 5 servers defined:
 *
 *  server1 is not monitored but listed in [RW Split Service]
 *  server5 is always stopped
 *
 *
 *
 *  MaxScale configuration:
 *
 *  [gateway]
 *  threads=4
 *
 *  [RW Split Service]
 #Please note server1 is not monitored n MySQL Monitor section
 *  type=service
 *  router=readwritesplit
 *  servers=server1,server2,server3,server5,server4
 *  max_slave_connections=100%
 *  max_slave_replication_lag=21
 *  user=massi
 *  passwd=massi
 *  enable_root_user=0
 *  filters=Hint
 *
 # Definition of the servers
 #  [server1]
 #not monitored
 #  type=server
 #  address=127.0.0.1
 #  port=3306
 #  protocol=MySQLBackend
 #
 #  [server2]
 #  type=server
 #  address=127.0.0.1
 #  port=3307
 #  protocol=MySQLBackend
 #
 #  [server3]
 #  type=server
 #  address=127.0.0.1
 #  port=3308
 #  protocol=MySQLBackend
 #
 #  [server4]
 #  type=server
 #  address=127.0.0.1
 #  port=3309
 #  protocol=MySQLBackend
 #
 #  [server5]
 #always stopped
 #  type=server
 #  address=127.0.0.1
 #  port=3310
 #  protocol=MySQLBackend
 #
 #
 #  [RW Split Listener]
 #  type=listener
 #  service=RW Split Service
 #  protocol=MySQLClient
 #  port=4606
 #
 #  [Hint]
 #  type=filter
 #  module=hintfilter
 #
 #  [MySQL Monitor]
 # Please note server1 is not monitored
 #  type=monitor
 #  module=mysqlmon
 #  servers=server4,server2,server3,server5
 #  user=massi
 #  passwd=massi
 #  detect_replication_lag=1
 #  monitor_interval=10001
 #
 #
 #
 #  Removing server1 from the service section gives right results for server_id selection
 #
 #
 #
 # mysql -c -h 127.0.0.1 -P 4606 -umassi -pmassi
 #  MariaDB> select @@server_id; -- maxscale route to server server4
 #
 #
 #
 #  Please not -c option that allows comments to be sent
 #
 #
 #  Vilho Raatikka 2014-08-08 08:13:42 UTC
 #  After further consideration we decided that the behavior is invalid. Routing hints should be followed if
 #they don't violate database consistency nor cluster setup.
 #  Comment 11 Vilho Raatikka 2014-08-08 17:26:25 UTC
 #  Pushed fix in commit d4de582e1622908cc95396f57878f8691289c35b to Z2.
 #  Replication lag is not checked if routing hint is used.
 #
 */



#include <iostream>
#include <unistd.h>
#include "testconnections.h"

using namespace std;

int main(int argc, char* argv[])
{
    TestConnections* Test = new TestConnections(argc, argv);
    Test->set_timeout(10);

    Test->repl->connect();
    Test->add_result(Test->maxscales->connect_maxscale(0), "Failed to connect to MaxScale\n");

    char server_id[256];
    char server_id_d[256];

    char hint_sql[64];

    for (int i = 1; i < 25; i++)
    {
        for (int j = 0; j < Test->repl->N; j++)
        {
            if (j != 1)
            {
                Test->set_timeout(5);
                sprintf(hint_sql, "select @@server_id; -- maxscale route to server server%d", j + 1);

                find_field(Test->maxscales->conn_rwsplit[0], hint_sql, (char*) "@@server_id", &server_id[0]);
                find_field(Test->repl->nodes[j],
                           (char*) "select @@server_id;",
                           (char*) "@@server_id",
                           &server_id_d[0]);

                Test->tprintf("server%d ID from Maxscale: \t%s\n", j + 1, server_id);
                Test->tprintf("server%d ID directly from node: \t%s\n", j + 1, server_id_d);

                Test->add_result(strcmp(server_id, server_id_d), "Hints does not work!\n");
            }
        }
    }

    Test->set_timeout(10);

    Test->maxscales->close_maxscale_connections(0);
    Test->repl->close_connections();

    Test->check_maxscale_alive(0);

    int rval = Test->global_result;
    delete Test;
    return rval;
}
