#!/bin/bash

# $1 - Socket connect flag for mysql, e.g. --socket=/tmp/blah
# $2 - Cluster type; "mariadb", "galera", "columnstore" or "xpand"

# The following environment variables are used:
# node_user     - A custom user to create
# node_password - The password for the user
# require_ssl   - Require SSL for all users except the replication user

if [ $# -ne 2 ]
then
    echo "usage: create_user.sh <socket> <type>"
    echo
    echo "where"
    echo
    echo "    socket: Socket flag to mysql, e.g. '--socket=/tmp/blah'"
    echo "    type  : One of 'mariadb', 'galera', 'columnstore' or 'xpand'"
    exit 1
fi

socket=$1
type=$2
version=`mysql -ss $socket -e "SELECT @@version"`
# version is now e.g. "10.4.12-MariaDB"

# Remove from first '-': "10.4.12-MariaDB" => "10.4.12"
version=${version%%-*}
# Remove from first '.': "10.4.12" => "10"
major=${version%%.*}
# Remove up until and including first '.': "10.4.12" => "4.12"
minor=${version#*.}
# Remove from first '.': "4.12" => "4"
minor=${minor%%.*}
# Remove up until and including last '.': "10.4.12" => "12"
maintenance=${version##*.}

echo type=$type
echo version=$version
echo major=$major
echo minor=$minor
echo maintenance=$maintenance
echo

##
## Default database
##

mysql --force $socket <<EOF

DROP DATABASE IF EXISTS test;
CREATE DATABASE test;
EOF

##
## MariaDB
##

if [ "$type" == "mariadb" ]
then
    echo Creating users specific for MariaDB.
fi

##
## Galera
##

if [ "$type" == "galera" ]
then
    echo Creating users specific for Galera.
fi

##
## Columnstore
##

if [ "$type" == "columnstore" ]
then
    echo Creating users specific for Columnstore.
fi

##
## Xpand
##

# For the time being we give the SUPER privilege, although it might
# be better to just add it when running the test that tests
# softfailing.

if [ "$type" == "xpand" ]
then
    echo Creating users specific for Xpand.

    mysql --force $socket <<EOF

    CREATE USER 'xpandmon'@'%' IDENTIFIED BY 'xpandmon'  $require_ssl;
    GRANT SELECT ON system.membership TO 'xpandmon'@'%';
    GRANT SELECT ON system.nodeinfo TO 'xpandmon'@'%';
    GRANT SELECT ON system.softfailed_nodes TO 'xpandmon'@'%';
    GRANT SUPER ON *.* TO 'xpandmon'@'%';
EOF
fi

##
## "Legacy" users.
##

mysql --force $socket <<EOF

DROP USER IF EXISTS '$node_user'@'%';
CREATE USER '$node_user'@'%' IDENTIFIED BY '$node_password' $require_ssl;
GRANT ALL ON *.* TO '$node_user'@'%' WITH GRANT OPTION;

DROP USER IF EXISTS 'repl'@'%';
CREATE USER 'repl'@'%' IDENTIFIED BY 'repl';
GRANT ALL ON *.* TO 'repl'@'%' WITH GRANT OPTION;

DROP USER IF EXISTS 'skysql'@'%';
CREATE USER 'skysql'@'%' IDENTIFIED BY 'skysql' $require_ssl;
GRANT ALL ON *.* TO 'skysql'@'%' WITH GRANT OPTION;

DROP USER IF EXISTS 'maxskysql'@'%';
CREATE USER 'maxskysql'@'%' IDENTIFIED BY 'skysql' $require_ssl;
GRANT ALL ON *.* TO 'maxskysql'@'%' WITH GRANT OPTION;

DROP USER IF EXISTS 'maxuser'@'%';
CREATE USER 'maxuser'@'%' IDENTIFIED BY 'maxpwd' $require_ssl;
GRANT ALL ON *.* TO 'maxuser'@'%' WITH GRANT OPTION;

RESET MASTER;
EOF
