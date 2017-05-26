# Results locations

| Location | Description |
|----------|-------------|
|[run_test](http://max-tst-01.mariadb.com:8089/view/test/job/run_test/) Jenkins job log|Vagrant and test application outputs|
|[CDash](jenkins.engskysql.com/CDash/index.php?project=MaxScale)|CTest reports|
|[http://max-tst-01.mariadb.com/LOGS/](http://max-tst-01.mariadb.com/LOGS/)|MaxScale logs and core dumps|
|/home/vagrant/LOGS|Same as [http://max-tst-01.mariadb.com/LOGS/](http://max-tst-01.mariadb.com/LOGS/)|
|Maxscale VM /var/log/maxscale|MaxScale log from latest test case|
|Maxscale VM /tpm/core*|Core dump from latest test case|
|Maxscale VM home directory|QLA filter files (if enabled in MaxScale test configuration|
|nodeN, galeraN VMs|MariaDB/MySQL logs (see MariaDB/MySQL documentation for details)|

For access to VMs see [environment documentation](ENV_SETUP.md#access-vms)

Jenkins job log consists of following parts:
* Vagrant output: VMs creation priocess, MariaDB Master/Slave and MariaDB Galera stuff installation, MaxScale installation
* [set_env_vagrant.sh](https://github.com/mariadb-corporation/build-scripts-vagrant/blob/master/test/set_env_vagrant.sh) output: retrieval of all VM parameters
* setup scripts output: MariaDB initialisation on backend nodes, DB users setup, enabling core dump on MaxScale VM
* test application output for all tests: eevry line starts from test case number and ':' (can be grepped)
* CTest final printing: N of M tests passed, CTest warnings, email sending logs

To check presence of core dumps:
<pre>
find /home/vagrant/LOGS/&lt;last_test_results_dir&gt; | grep core
</pre>

where 'last_test_results_dir' - automatically generated name of logs directory (based on date and time of test run)

To understand test case output please see test case description in Doxygen comments in every test case source file.

VMs are alive after the test run only if test run is done with 'do_not_destroy' parameter.
