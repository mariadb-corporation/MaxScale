#!/bin/bash

echo "DROP USER '$node_user'@'%'" | sudo mysql
echo "grant all privileges on *.*  to '$node_user'@'%' identified by '$node_password' require ssl WITH GRANT OPTION" 
echo "grant all privileges on *.*  to '$node_user'@'%' identified by '$node_password' require ssl WITH GRANT OPTION" | sudo mysql
