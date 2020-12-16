#!/bin/bash

# The following environment variables are used:
# node_user     - A custom user to create
# node_password - The password for the user

# Wait until the node is ready

for ((i=0;i<100;i++))
do
    mysql -ss $1 -e 'show status like "wsrep_ready"' | grep 'ON' && break || sleep 1
done

# The removal of the anonymous ''@'localhost' user is done in a somewhat crude
# way. The proper way would be to do a secure installation or drop the users
# with DROP USER statements.

mysql --force <<EOF

CREATE DATABASE IF NOT EXISTS test;

DELETE FROM mysql.user WHERE user = '';

DROP USER IF EXISTS '$node_user'@'%';
CREATE USER '$node_user'@'%' IDENTIFIED BY '$node_password';
GRANT ALL PRIVILEGES ON *.* TO '$node_user'@'%' WITH GRANT OPTION;

DROP USER IF EXISTS 'maxskysql'@'%';
CREATE USER 'maxskysql'@'%' IDENTIFIED BY 'skysql';
GRANT ALL PRIVILEGES ON *.* TO 'maxskysql'@'%' WITH GRANT OPTION;

DROP USER IF EXISTS 'repl'@'%';
CREATE USER 'repl'@'%' IDENTIFIED BY 'repl';
GRANT ALL PRIVILEGES ON *.* TO 'repl'@'%' WITH GRANT OPTION;

DROP USER IF EXISTS 'skysql'@'%';
CREATE USER 'skysql'@'%' IDENTIFIED BY 'skysql';
GRANT ALL PRIVILEGES ON *.* TO 'skysql'@'%' WITH GRANT OPTION;

DROP USER IF EXISTS 'maxuser'@'%';
CREATE USER 'maxuser'@'%' IDENTIFIED BY 'maxpwd';
GRANT ALL ON *.* TO 'maxuser'@'%' WITH GRANT OPTION;

DROP DATABASE IF EXISTS test;
CREATE DATABASE test;

EOF
