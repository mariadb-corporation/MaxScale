/**
 * @file bug729.cpp regression case for bug 729 ("PDO prepared statements bug introduced")
 *
 * - execute following PHP script
 * @verbatim
<?php
$options = [
    PDO::ATTR_ERRMODE => PDO::ERRMODE_EXCEPTION,
    PDO::ATTR_EMULATE_PREPARES => false,
];

$host=$argv[1];
$port=$argv[2];
$user=$argv[3];
$pass=$argv[4];

$dsn = "mysql:host=".$host.";port=".$port.";dbname=information_schema";
$dbh = new PDO( $dsn, $user, $pass, $options );
$res = $dbh
    ->query( "SELECT COLLATION_NAME FROM COLLATIONS" )
    ->fetch( PDO::FETCH_COLUMN );

var_dump( $res );

 @endverbatim
 * - check log for "Can't route MYSQL_COM_STMT_PREPARE"
 */

/*

Description Andreas K-Hansen 2015-02-12 19:32:13 UTC

The error occurred when upgrading from Maxscale 1.0.4 to 1.0.5.
The following exception occurs when trying to execute a query with prepared statements enabled:

PHP Fatal error:  Uncaught exception 'PDOException' with message 'SQLSTATE[42000]: Syntax error or access violation: 1064 Routing query to backend failed. See the error log for further details.' in /root/test.php:10
Stack trace:
#0 /root/test.php(10): PDO->query('SELECT COLLATIO...')
#1 {main}
  thrown in /root/test.php on line 10

- Error log
Feb 12 19:14:01 363d6aec0f8c MaxScale[263]: Error: Failed to obtain address for host ::1, Address family for hostname not supported
Feb 12 19:14:01 363d6aec0f8c MaxScale[263]: Warning: Failed to add user root@::1 for service [RW Split Router]. This user will be unavailable via MaxScale.
Feb 12 19:14:01 363d6aec0f8c MaxScale[263]: Warning: Failed to add user root@127.0.0.1 for service [RW Split Router]. This user will be unavailable via MaxScale.
Feb 12 19:14:10 363d6aec0f8c MaxScale[263]: Warning : The query can't be routed to all backend servers because it includes SELECT and SQL variable modifications which is not supported. Set use_sql_variables_in=master or split the query to two, where SQL variable modifications are done in the first and the SELECT in the second one.
Feb 12 19:14:10 363d6aec0f8c MaxScale[263]: Error : Can't route MYSQL_COM_STMT_PREPARE:QUERY_TYPE_READ|QUERY_TYPE_PREPARE_STMT:"MYSQL_COM_STMT_PREPARE". SELECT with session data modification is not supported if configuration parameter use_sql_variables_in=all .

*/


#include <iostream>
#include "testconnections.h"

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);
    Test->set_timeout(30);
    char str[1024];

    sprintf(str, "php %s/bug729.php %s %d %s %s", test_dir, Test->maxscale_IP, Test->rwsplit_port,
            Test->maxscale_user, Test->maxscale_password);

    Test->tprintf("Executing PHP script: %s\n", str);
    Test->add_result(system(str), "PHP script FAILED!\n");

    Test->check_log_err((char *) "Can't route MYSQL_COM_STMT_PREPARE", false);

    int rval = Test->global_result;
    delete Test;
    return rval;
}
