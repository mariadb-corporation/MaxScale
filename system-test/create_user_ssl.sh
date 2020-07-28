#!/bin/bash

echo "DROP USER '$node_user'@'%'" | sudo mysql $1
echo "grant all privileges on *.*  to '$node_user'@'%' identified by '$node_password' require ssl WITH GRANT OPTION"
echo "grant all privileges on *.*  to '$node_user'@'%' identified by '$node_password' require ssl WITH GRANT OPTION" | sudo mysql $1

echo "grant all privileges on *.*  to 'maxskysql'@'%' identified by 'skysql'  require ssl WITH GRANT OPTION" | sudo mysql $1
echo "grant all privileges on *.*  to 'maxuser'@'%' identified by 'maxpwd'  require ssl WITH GRANT OPTION" | sudo mysql $1
