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
version_response=`mysql -ss $socket -e "SELECT @@version"`
version=$version_response
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

function is_integer() {
    [[ ${1} == ?(-)+([0-9]) ]]
}

if ! is_integer $major || ! is_integer $minor || ! is_integer $maintenance
then
    echo error: \"$version_response\" does not appear to be a valid MariaDB version string.
    exit 1
fi

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

##
## MariaDB
##

if [ "$type" == "mariadb" ]
then
    echo Creating users specific for MariaDB.

    mysql --force $socket <<EOF
    CREATE USER 'mariadbmon'@'%' IDENTIFIED BY 'mariadbmon' $require_ssl;

    GRANT SUPER, RELOAD, PROCESS, SHOW DATABASES, EVENT ON *.* TO 'mariadbmon'@'%';
    GRANT SELECT ON mysql.user TO 'mariadbmon'@'%';
EOF

    if (( $major == 10 && $minor >= 5 ))
    then
        mysql --force $socket <<EOF
        GRANT REPLICATION SLAVE ADMIN ON *.* TO 'mariadbmon'@'%';
EOF
    else
        mysql --force $socket <<EOF
        GRANT REPLICATION CLIENT ON *.* TO 'mariadbmon'@'%';
EOF
    fi

    if (( $major == 10 && (($minor == 5 && $maintenance >= 8) || ( $minor >=6 )) ))
    then
        # So, since 10.5.8 we have this.
        mysql --force $socket <<EOF
        GRANT REPLICATION SLAVE, SLAVE MONITOR ON *.* TO 'repl'@'%';
EOF
    else
        mysql --force $socket <<EOF
        GRANT REPLICATION SLAVE ON *.* TO 'repl'@'%';
EOF
    fi
fi

##
## Galera
##

if [ "$type" == "galera" ]
then
    echo Creating users specific for Galera.

    # SUPER needed if 'set_donor_nodes' is configured. Might be better
    # to give that grant explicitly in tests that actually need that.

    # TODO: Should REPLICATION CLIENT be REPLICATION SLAVE, SLAVE MONITOR
    # TODO: here as well if version >= 10.5.8.

    mysql --force $socket <<EOF
    CREATE USER 'galeramon'@'%' IDENTIFIED BY 'galeramon' $require_ssl;
    GRANT REPLICATION CLIENT ON *.* TO 'galeramon'@'%';
    GRANT SUPER ON *.* TO 'galeramon'@'%';
EOF

fi

##
## Columnstore
##

if [ "$type" == "columnstore" ]
then
    echo Creating users specific for Columnstore.

    mysql --force $socket <<EOF
    CREATE USER 'csmon'@'%' IDENTIFIED BY 'csmon'  $require_ssl;
    GRANT ALL ON infinidb_vtable.* TO 'csmon'@'%';
EOF

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
