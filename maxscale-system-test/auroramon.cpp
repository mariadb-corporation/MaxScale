/**
 * @file auroramon.cpp test of Aurora RDS monitor
 * - create RDS cluster
 * - find 'writer' node and uses 'maxadmin' to check that this node is "Master, Running"
 * - do forced failover
 * - find 'writer' again and repeat check
 * - destroy RDS cluster
 */


#include "testconnections.h"
#include "execute_cmd.h"
#include "rds_vpc.h"

int set_endspoints(RDS * cluster)
{

    json_t *endpoint;
    long long int port;
    const char * IP;
    char p[64];
    size_t i;
    char cmd[1024];

    json_t * endpoints = cluster->get_endpoints();
    if (endpoints == NULL)
    {
        return -1;
    }

    json_array_foreach(endpoints, i, endpoint)
    {
        port = json_integer_value(json_object_get(endpoint, "Port"));
        IP = json_string_value(json_object_get(endpoint, "Address"));
        printf("host: %s \t port: %lld\n", IP, port);
        sprintf(cmd, "node_%03d_network", (int) i);
        setenv(cmd, IP, 1);
        sprintf(cmd, "node_%03d_port", (int) i);
        sprintf(p, "%lld", port);
        setenv(cmd, p, 1);
    }

    setenv("node_password", "skysqlrds", 1);
    setenv("maxscales->user_name", "skysql", 1);
    setenv("maxscales->password", "skysqlrds", 1);
    setenv("no_nodes_check", "yes", 1);
    setenv("no_backend_log_copy", "yes", 1);
    return 0;
}


void compare_masters(TestConnections* Test, RDS * cluster)
{
    const char * aurora_master;
    cluster->get_writer(&aurora_master);
    Test->tprintf("Aurora writer node: %s\n", aurora_master);
    char maxadmin_status[1024];
    int i;
    char cmd[1024];
    for (i = 0; i < Test->repl->N; i++)
    {
        sprintf(cmd, "show server server%d", i + 1);
        Test->maxscales->get_maxadmin_param(0, cmd, (char *) "Status:", &maxadmin_status[0]);
        Test->tprintf("Server%d status %s\n", i + 1, maxadmin_status);
        sprintf(cmd, "node%03d", i);
        if (strcmp(aurora_master, cmd) == 0)
        {
            if (strcmp(maxadmin_status, "Master, Running"))
            {
                Test->tprintf("Maxadmin reports node%03d is a Master as expected", i);
            }
            else
            {
                Test->add_result(1, "Server node%03d status is not 'Master, Running'', it is '%s'", i, maxadmin_status);
            }
        }
        else
        {
            if (strcmp(maxadmin_status, "Slave, Running"))
            {
                Test->tprintf("Maxadmin reports node%03d is a Slave as expected", i);
            }
            else
            {
                Test->add_result(1, "Server node%03d status is not 'Slave, Running'', it is '%s'", i, maxadmin_status);
            }
        }

    }
}

int main(int argc, char *argv[])
{
    RDS * cluster = new RDS((char *) "auroratest");

    if (cluster->create_rds_db(4) != 0)
    {
        printf("Error RDS creation\n");
        return 1;
    }
    cluster->wait_for_nodes(4);


    if (set_endspoints(cluster) != 0)
    {
        printf("Error getting RDS endpoints\n");
        return 1;
    }


    TestConnections * Test = new TestConnections(argc, argv);
    Test->set_timeout(30);

    compare_masters(Test, cluster);

    Test->set_timeout(30);
    Test->tprintf("Executing a query through readwritesplit before failover");
    Test->maxscales->connect_rwsplit(0);
    Test->try_query(Test->maxscales->conn_rwsplit[0][0], "show processlist");
    char server_id[1024];
    Test->tprintf("Get aurora_server_id\n");
    find_field(Test->maxscales->conn_rwsplit[0][0], "select @@aurora_server_id;", "server_id", &server_id[0]);
    Test->maxscales->close_rwsplit(0);
    Test->tprintf("server_id before failover: %s\n", server_id);

    Test->stop_timeout();
    Test->tprintf("Performing cluster failover\n");

    Test->add_result(cluster->do_failover(), "Failover failed\n");

    Test->tprintf("Failover done\n");

    // Do the failover here and wait until it is over
    //sleep(10);

    Test->set_timeout(30);
    Test->tprintf("Executing a query through readwritesplit after failover");
    Test->maxscales->connect_rwsplit(0);
    Test->try_query(Test->maxscales->conn_rwsplit[0][0], "show processlist");
    Test->tprintf("Get aurora_server_id\n");
    find_field(Test->maxscales->conn_rwsplit[0][0], "select @@aurora_server_id;", "server_id", &server_id[0]);
    Test->maxscales->close_rwsplit(0);
    Test->tprintf("server_id after failover: %s\n", server_id);

    compare_masters(Test, cluster);


    //Test->check_maxscale_alive(0);


    Test->stop_timeout();
    cluster->delete_rds_cluster();

    int rval = Test->global_result;
    delete Test;
    return rval;
}
