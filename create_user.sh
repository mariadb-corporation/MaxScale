#!/bin/bash

echo "DROP USER '$node_user'@'%'" | sudo mysql
echo "grant all privileges on *.*  to '$node_user'@'%' identified by '$node_password' WITH GRANT OPTION" 
echo "grant all privileges on *.*  to '$node_user'@'%' identified by '$node_password' WITH GRANT OPTION" | sudo mysql
