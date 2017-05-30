/**
 * @file bug143.cpp bug143 regression case (MaxScale ignores host in user authentication)
 *
 * - create  user@'non_existing_host1', user@'%', user@'non_existing_host2' identified by different passwords.
 * - try to connect using RWSplit. First and third are expected to fail, second - succseed.
 */


#include <my_config.h>
#include <iostream>
#include "testconnections.h"

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);

    Test->read_env();
    Test->print_env();
    Test->set_timeout(5);
    Test->repl->connect();
    Test->connect_maxscale();

    Test->tprintf("Creating user 'user' with 3 different passwords for different hosts\n");
    execute_query(Test->conn_rwsplit, (char *) "GRANT ALL PRIVILEGES ON *.* TO user@'non_existing_host1' identified by 'pass1';  FLUSH PRIVILEGES;");
    execute_query(Test->conn_rwsplit, (char *) "GRANT ALL PRIVILEGES ON *.* TO user@'%'  identified by 'pass2';  FLUSH PRIVILEGES;");
    execute_query(Test->conn_rwsplit, (char *) "GRANT ALL PRIVILEGES ON *.* TO user@'non_existing_host2' identified by 'pass3';  FLUSH PRIVILEGES;");

    printf("sleeping 20 seconds to let replication happen\n");  fflush(stdout);
    Test->set_timeout(50);
    sleep(20);

    Test->set_timeout(5);
    MYSQL * conn = open_conn(Test->rwsplit_port, Test->maxscale_IP, (char *) "user", (char *) "pass1", Test->ssl);
    if (mysql_errno(conn) == 0) {
        Test->add_result(1, "MaxScale ignores host in authentification\n");
    }
    if (conn != NULL) {mysql_close(conn);}

    conn = open_conn(Test->rwsplit_port, Test->maxscale_IP, (char *) "user", (char *) "pass2", Test->ssl);
    Test->add_result(mysql_errno(conn), "MaxScale can't connect: %s\n", mysql_error(conn));
    if (conn != NULL) {mysql_close(conn);}

    conn = open_conn(Test->rwsplit_port, Test->maxscale_IP, (char *) "user", (char *) "pass3", Test->ssl);
    if (mysql_errno(conn) == 0) {
        Test->add_result(1, "MaxScale ignores host in authentification\n");
    }
    if (conn != NULL) {mysql_close(conn);}

    execute_query(Test->conn_rwsplit, (char *) "DROP USER user@'%';");
    execute_query(Test->conn_rwsplit, (char *) "DROP USER user@'non_existing_host1';");
    execute_query(Test->conn_rwsplit, (char *) "DROP USER user@'non_existing_host2';");
    Test->close_maxscale_connections();

    Test->copy_all_logs(); return(Test->global_result);

}
