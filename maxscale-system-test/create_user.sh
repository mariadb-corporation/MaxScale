#!/bin/bash

mysql -u root --force $1 <<EOF >& /dev/null

DROP USER '$node_user'@'%';
CREATE USER '$node_user'@'%' IDENTIFIED BY '$node_password';
GRANT ALL PRIVILEGES ON *.* TO '$node_user'@'%' WITH GRANT OPTION;

DROP USER 'repl'@'%';
CREATE USER 'repl'@'%' IDENTIFIED BY 'repl';
GRANT ALL ON *.* TO 'repl'@'%' WITH GRANT OPTION;

DROP USER 'repl'@'localhost';
CREATE USER 'repl'@'localhost' IDENTIFIED BY 'repl';
GRANT ALL ON *.* TO 'repl'@'localhost' WITH GRANT OPTION;

DROP USER 'skysql'@'%';
CREATE USER 'skysql'@'%' IDENTIFIED BY 'skysql';
GRANT ALL ON *.* TO 'skysql'@'%' WITH GRANT OPTION;

DROP USER 'skysql'@'localhost';
CREATE USER 'skysql'@'localhost' IDENTIFIED BY 'skysql';
GRANT ALL ON *.* TO 'skysql'@'localhost' WITH GRANT OPTION;

DROP USER 'maxskysql'@'%';
CREATE USER 'maxskysql'@'%' IDENTIFIED BY 'skysql';
GRANT ALL ON *.* TO 'maxskysql'@'%' WITH GRANT OPTION;

DROP USER 'maxskysql'@'localhost';
CREATE USER 'maxskysql'@'localhost' IDENTIFIED BY 'skysql';
GRANT ALL ON *.* TO 'maxskysql'@'localhost' WITH GRANT OPTION;

RESET MASTER;
EOF
