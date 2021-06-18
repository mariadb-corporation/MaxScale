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
## "Legacy" users. The "test-admin"-user is used by the test system itself to manage clusters and should
## never require ssl. Tests should not remove or modify it.
##

mysql --force $socket <<EOF

DROP USER IF EXISTS 'test-admin'@'%';
CREATE USER 'test-admin'@'%' IDENTIFIED BY 'test-admin-pw';
GRANT ALL ON *.* TO 'test-admin'@'%' WITH GRANT OPTION;

DROP USER IF EXISTS '$node_user'@'%';
CREATE USER '$node_user'@'%' IDENTIFIED BY '$node_password' $require_ssl;
GRANT ALL ON *.* TO '$node_user'@'%' WITH GRANT OPTION;

EOF


##
## Columnstore
##

if [ "$type" == "columnstore" ]
then
    echo Creating users specific for Columnstore.

    mysql --force $socket <<EOF
    DROP USER IF EXISTS 'csmon'@'%';
    CREATE USER 'csmon'@'%' IDENTIFIED BY 'csmon'  $require_ssl;
    GRANT ALL ON infinidb_vtable.* TO 'csmon'@'%';

    GRANT ALL ON infinidb_vtable.* TO 'maxservice'@'%';
EOF

fi
