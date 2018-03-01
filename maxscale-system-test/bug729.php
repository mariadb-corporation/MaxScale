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

