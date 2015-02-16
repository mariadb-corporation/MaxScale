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
 * - check log for "Error : Loading database names for service RW_Split encountered error: Unknown column"
 */

#include <my_config.h>
#include <iostream>
#include "testconnections.h"

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argv[0]);
    int global_result = 0;
    char str[1024];

    Test->ReadEnv();
    Test->PrintIP();

    sprintf(str, "php %s/bug729.php %s %d %s %s", Test->test_dir, Test->Maxscale_IP, Test->rwsplit_port, Test->Maxscale_User, Test->Maxscale_Password);

    printf("Executing PHP script: %s\n", str); fflush(stdout);
    if (system(str) !=0) {
        global_result++;
        printf("PHP script FAILED!\n"); fflush(stdout);
    }

    global_result += CheckLogErr((char *) "Error : Can't route MYSQL_COM_STMT_PREPARE", FALSE);

    exit(global_result);
}
