#!/bin/bash

# Wait until the node is ready

for ((i=0;i<100;i++))
do
    mysql -ss -u root $1 -e 'show status like "wsrep_ready"' | grep 'ON' && break || sleep 1
done

# The removal of the anonymous ''@'localhost' user is done in a somewhat crude
# way. The proper way would be to do a secure installation or drop the users
# with DROP USER statements.

mysql -u root --force <<EOF

DELETE FROM mysql.user WHERE user = '';

DROP USER IF EXISTS '$galera_user'@'%';
CREATE USER '$galera_user'@'%' IDENTIFIED BY '$galera_password';
GRANT ALL PRIVILEGES ON *.* TO '$galera_user'@'%' WITH GRANT OPTION;

DROP USER IF EXISTS 'maxskysql'@'%';
CREATE USER 'maxskysql'@'%' IDENTIFIED BY 'skysql';
GRANT ALL PRIVILEGES ON *.* TO 'maxskysql'@'%' WITH GRANT OPTION;

DROP USER IF EXISTS 'maxskysql'@'localhost';
CREATE USER 'maxskysql'@'localhost' IDENTIFIED BY 'skysql';
GRANT ALL ON *.* TO 'maxskysql'@'localhost' WITH GRANT OPTION;

DROP USER IF EXISTS 'repl'@'%';
CREATE USER 'repl'@'%' IDENTIFIED BY 'repl';
GRANT ALL PRIVILEGES ON *.* TO 'repl'@'%' WITH GRANT OPTION;

DROP USER IF EXISTS 'skysql'@'%';
CREATE USER 'skysql'@'%' IDENTIFIED BY 'skysql';
GRANT ALL PRIVILEGES ON *.* TO 'skysql'@'%' WITH GRANT OPTION;

DROP USER IF EXISTS 'skysql'@'localhost';
CREATE USER 'skysql'@'localhost' IDENTIFIED BY 'skysql';
GRANT ALL PRIVILEGES ON *.* TO 'skysql'@'localhost' WITH GRANT OPTION;

DROP USER IF EXISTS 'maxuser'@'%';
CREATE USER 'maxuser'@'%' IDENTIFIED BY 'maxpwd';
GRANT ALL ON *.* TO 'maxuser'@'%' WITH GRANT OPTION;

DROP USER IF EXISTS 'maxuser'@'localhost';
CREATE USER 'maxuser'@'localhost' IDENTIFIED BY 'maxpwd';
GRANT ALL ON *.* TO 'maxuser'@'localhost' WITH GRANT OPTION;


DROP DATABASE IF EXISTS test;
CREATE DATABASE test;

EOF
