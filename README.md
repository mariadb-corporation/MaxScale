# maxscale-system-test
System level tests for MaxScale

## Basics
- every test is separate executable file
- backend for test:
  - 1 machine for Maxscale
  - >= 4 machines for Master/Slave
  - >= 4 machines for Galera cluster
- environmental variables contains all information about backend: IPs, user names, passwords, paths to tools, etc
- backed can be created with help of [MDBCI tool](https://github.com/OSLL/mdbci)
- configuring of Master/Slave and Galera can be done with help of [build scripts package](https://github.com/mariadb-corporation/build-scripts-vagrant)

## Manuals

[How to run tests with existing backend](https://github.com/mariadb-corporation/build-scripts-vagrant/blob/master/RUN_TEST.md#running-tests-with-existing-test-configuration)

Hints: How to write a test

## Environmental variables
|variable|meaning|
|--------|-------|
|repl_N|Number of machines for Master/Slave|
|repl_XXX|IP address of Master/Slave machine number XXX|
|repl_private_XXX|private IP address of Master/Slave machine XXX for AWS machines (for everything else - same as repl_XXX|
|repl_port_XXX|MariaDB port of Master/Slave machine XXX|
|repl_access_user_XXX|user name to access Master/Slave machine XXX via ssh|
|repl_access_sudo_XXX|'sudo ' if repl_access_user_XXX does not have root rights, empty string if repl_access_user_XXX has root rights|
|repl_sshkey_XXX|full name of secret key to access Master/Slave machine XXX via ssh|
|repl_start_db_command_XXX|bash command to start DB server on Master/Slave machine XXX|
|repl_stop_db_command_XXX|bash command to stop DB server on Master/Slave machine XXX|
|repl_user|DB user name to access Master/Slave nodes (have to have all priveligies with GRANT option)|
|repl_password|password for repl_user|
|galera_N|Number of machines for Galera|
|galera_XXX|IP address of Galera machine number XXX|
|galera_private_XXX|private IP address of Galera machine XXX for AWS machines (for everything else - same as repl_XXX|
|galera_port_XXX|MariaDB port of Galera machine XXX|
|galera_access_user_XXX|user name to access Galera machine XXX via ssh|
|galera_access_sudo_XXX|'sudo ' if repl_access_user_XXX does not have root rights, empty string if repl_access_user_XXX has root rights|
|galera_sshkey_XXX|full name of secret key to access Galera machine XXX via ssh|
|galera_start_db_command_XXX|bash command to start DB server on Galera machine XXX|
|galera_stop_db_command_XXX|bash command to stop DB server on Galera machine XXX|
|galera_user|DB user name to access Galera nodes (have to have all priveligies with GRANT option)|
|galera_password|password for repl_user|
|maxdir|Maxscale home dir|
|maxdir_bin|path to Maxscale binary files|
|maxscale_cnf|full name of Maxscale configuration file (maxscale.cnf)|
|maxscale_log_dir|directory for Maxscale log files|
|maxscale_IP|IP address of Maxscale machine|
|maxscale_sshkey|full name of secret key to access Maxscale machine via ssh|
|maxscale_access_user|user name to access Maxscale machine via ssh|
|maxscale_access_sudo|'sudo ' if maxscale_access_user does not have root rights, empty string if maxscale_access_user has root rights|
|maxscale_user|DB user to access via Maxscale|
|maxscale_password|password for maxscale_user|
|maxscale_hostname|hostname of Maxscale machine|
|get_logs_command|bash command to copy logs from Maxscale machine|
|sysbench_dir|directory where Sysbanch is installed|
|ssl|'yes' if tests should try to use ssl to connect to Maxscale and to backends (obsolete, now should be 'yes' in all cases)|
|smoke|if 'yes' all tests are executed in 'quick' mode (less iterations, skip heavy operations)|
