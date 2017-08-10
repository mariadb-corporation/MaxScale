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

[How to run tests](https://github.com/mariadb-corporation/build-scripts-vagrant/blob/master/RUN_TEST.md)

[Hints: How to write a test](HOW_TO_WRITE_TEST.md)

[Build and test environment setup (if you want to play with MDBCI and Vagrant on your local machine](ENV_SETUP.md)

[Jenkins instructions](JENKINS.md)

## Environmental variables
|variable|meaning|
|--------|-------|
|node_N|Number of machines for Master/Slave|
|node_XXX_network|IP address of Master/Slave machine number XXX|
|node_XXX_private_ip|private IP address of Master/Slave machine XXX for AWS machines (for everything else - same as node_XXX|
|node_XXX_port|MariaDB port of Master/Slave machine XXX|
|node_XXX_whoami|user name to access Master/Slave machine XXX via ssh|
|node_XXX_access_sudo|'sudo ' if node_access_user_XXX does not have root rights, empty string if node_access_user_XXX has root rights|
|node_XXX_keyfile|full name of secret key to access Master/Slave machine XXX via ssh|
|node_XXX_start_db_command|bash command to start DB server on Master/Slave machine XXX|
|node_XXX_stop_db_command|bash command to stop DB server on Master/Slave machine XXX|
|node_user|DB user name to access Master/Slave nodes (have to have all priveligies with GRANT option)|
|node_password|password for node_user|
|galera_N|Number of machines for Galera|
|galera_XXX_network|IP address of Galera machine number XXX|
|galera_XXX_private|private IP address of Galera machine XXX for AWS machines (for everything else - same as node_XXX|
|galera_XXX_port|MariaDB port of Galera machine XXX|
|galera_XXX_whoami|user name to access Galera machine XXX via ssh|
|galera_XXX_access|'sudo ' if node_access_user_XXX does not have root rights, empty string if node_access_user_XXX has root rights|
|galera_XXX_keyfile|full name of secret key to access Galera machine XXX via ssh|
|galera_XXX_start_db_command|bash command to start DB server on Galera machine XXX|
|galera_XXX_stop_db_command|bash command to stop DB server on Galera machine XXX|
|galera_user|DB user name to access Galera nodes (have to have all priveligies with GRANT option)|
|galera_password|password for node_user|
|maxscale_cnf|full name of Maxscale configuration file (maxscale.cnf)|
|maxscale_log_dir|directory for Maxscale log files|
|maxscale_IP|IP address of Maxscale machine|
|maxscale_sshkey|full name of secret key to access Maxscale machine via ssh|
|maxscale_access_user|user name to access Maxscale machine via ssh|
|maxscale_access_sudo|'sudo ' if maxscale_access_user does not have root rights, empty string if maxscale_access_user has root rights|
|maxscale_user|DB user to access via Maxscale|
|maxscale_password|password for maxscale_user|
|maxscale_hostname|hostname of Maxscale machine|
|sysbench_dir|directory where Sysbanch is installed|
|ssl|'yes' if tests should try to use ssl to connect to Maxscale and to backends (obsolete, now should be 'yes' in all cases)|
|smoke|if 'yes' all tests are executed in 'quick' mode (less iterations, skip heavy operations)|
|backend_ssl|if 'yes' ssl config will be added to all servers definition in maxscale.cnf|
|use_snapshots|if TRUE every test is trying to revert snapshot before running the test|
|take_snapshot_command|revert_snapshot_command|
|revert_snapshot_command|Command line to revert a snapshot of all VMs|
|no_nodes_check|if yes backend checks are not executed (needed in case of RDS or similar backend)|
|no_backend_log_copy|if yes logs from backend nodes are not copied (needed in case of RDS or similar backend)|
|no_maxscale_start|Do not start Maxscale automatically|
|no_vm_revert|If true tests do not revert VMs after the test even if test failed|
