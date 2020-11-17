#!/bin/bash

# The following environment variables are used:
# node_user     - A custom user to create
# node_password - The password for the user
# require_ssl   - Require SSL for all users except the replication user

mysql --force $1 <<EOF >& /dev/null

DROP USER IF EXISTS '$node_user'@'%';
CREATE USER '$node_user'@'%' IDENTIFIED BY '$node_password';
GRANT ALL PRIVILEGES ON *.* TO '$node_user'@'%' $require_ssl WITH GRANT OPTION;

DROP USER IF EXISTS 'repl'@'%';
CREATE USER 'repl'@'%' IDENTIFIED BY 'repl';
GRANT ALL ON *.* TO 'repl'@'%' WITH GRANT OPTION;

DROP USER IF EXISTS 'repl'@'localhost';
CREATE USER 'repl'@'localhost' IDENTIFIED BY 'repl';
GRANT ALL ON *.* TO 'repl'@'localhost' WITH GRANT OPTION;

DROP USER IF EXISTS 'skysql'@'%';
CREATE USER 'skysql'@'%' IDENTIFIED BY 'skysql';
GRANT ALL ON *.* TO 'skysql'@'%' $require_ssl WITH GRANT OPTION;

DROP USER IF EXISTS 'skysql'@'localhost';

DROP USER IF EXISTS 'maxskysql'@'%';
CREATE USER 'maxskysql'@'%' IDENTIFIED BY 'skysql';
GRANT ALL ON *.* TO 'maxskysql'@'%' $require_ssl WITH GRANT OPTION;

DROP USER IF EXISTS 'maxskysql'@'localhost';
CREATE USER 'maxskysql'@'localhost' IDENTIFIED BY 'skysql';
GRANT ALL ON *.* TO 'maxskysql'@'localhost' $require_ssl WITH GRANT OPTION;

DROP USER IF EXISTS 'maxuser'@'%';
CREATE USER 'maxuser'@'%' IDENTIFIED BY 'maxpwd';
GRANT ALL ON *.* TO 'maxuser'@'%' $require_ssl WITH GRANT OPTION;

DROP USER IF EXISTS 'maxuser'@'localhost';
CREATE USER 'maxuser'@'localhost' IDENTIFIED BY 'maxpwd';
GRANT ALL ON *.* TO 'maxuser'@'localhost' $require_ssl WITH GRANT OPTION;

RESET MASTER;
EOF
