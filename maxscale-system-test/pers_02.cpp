/**
 * @file pers_02.cpp - Persistent connection tests - crash during Maxscale restart
 * configuration:
 * @verbatim
[server1]
type=server
address=54.78.193.99
port=3306
protocol=MySQLBackend
persistpoolmax=1
persistmaxtime=3660

[server2]
type=server
address=54.78.254.183
port=3306
protocol=MySQLBackend
persistpoolmax=5
persistmaxtime=60

[server3]
type=server
address=54.78.217.99
port=3306
protocol=MySQLBackend
persistpoolmax=10
persistmaxtime=60

[server4]
type=server
address=176.34.202.107
port=3306
protocol=MySQLBackend
persistpoolmax=30
persistmaxtime=30

@endverbatim
 * open 75 connections to all Maxscale services
 * close connections
 * restart replication (stop all nodes and start them again, execute CHANGE MASTER TO again)
 * open 70 connections to all Maxscale services
 * close connections
 * check there is not crash during restart
 */

#include <my_config.h>
#include "testconnections.h"
#include "maxadmin_operations.h"


int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);
    Test->set_timeout(60);

    Test->repl->execute_query_all_nodes((char *) "set global max_connections = 20;");
    Test->create_connections(75, true, true, true, true);
    Test->set_timeout(100);
    Test->repl->start_replication();
    Test->repl->execute_query_all_nodes((char *) "set global max_connections = 200;");
    Test->set_timeout(60);
    Test->add_result(Test->create_connections(70 , true, true, true, true),
                     "Connections creation error \n");
    //execute_maxadmin_command(Test->maxscale_IP, (char *) "admin", Test->maxadmin_password, (char *) "flush logs");
    sleep(15);

    Test->repl->execute_query_all_nodes((char *) "set global max_connections = 100;");

    Test->check_log_err((char *) "fatal signal 11", false);
    Test->copy_all_logs(); return(Test->global_result);
}
