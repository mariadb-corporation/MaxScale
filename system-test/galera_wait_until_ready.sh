#!/bin/bash

# Wait until the node is ready

for ((i=0;i<100;i++))
do
    mysql -ss $1 -e 'show status like "wsrep_ready"' | grep 'ON' && break || sleep 1
done
