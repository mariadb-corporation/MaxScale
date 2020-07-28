set -x

dir=`pwd`


#cp ~/build-scripts/test/multiple_servers.cnf $dir
sudo killall mysqld
sudo killall mysql_install_db
sleep 10
rm -rf /data/mysql/mysql$1
rm -rf /var/log/mysql/*
mkdir -p /data/mysql/mysql$1
chown mysql:mysql -R /data
chown mysql:mysql -R /var/run/mysqld

mysql_install_db --defaults-file=$dir/local_tests/multiple_servers.cnf --user=mysql --datadir=/data/mysql/mysql$1
