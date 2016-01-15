# Jenkins 

## List of Jenkins

| URL | Description |
|----|----|
|[max-tst-01.mariadb.com:8089](http://max-tst-01.mariadb.com:8089)|AWS, qemu; Regular testing for different MariaDB versions, different Linux distributions, Developers testing|
|[maxscale-jenkins.mariadb.com:8089/](http://maxscale-jenkins.mariadb.com:8089/)|AWS, VBox; Regular builds for all distributions, build for coverity, regular test VBox+CentOS6+MariaDB5.5|
|[maxscale-jenkins.mariadb.com:8090](http://maxscale-jenkins.mariadb.com:8090/)|MDBCI testing and debugging, Jenkins experiments|

## Basic Jenkins jobs 

### [max-tst-01.mariadb.com:8089](http://max-tst-01.mariadb.com:8089)

| Job | Description |
|----|----|
|[build_and_test](http://max-tst-01.mariadb.com:8089/view/test/job/build_and_test/)|Build Maxscale and run systems tests|
|[run_test](http://max-tst-01.mariadb.com:8089/view/test/job/run_test/)|Run system tests, Maxscale package should be in the repository|
|[run_test_no_env_rebuild](http://max-tst-01.mariadb.com:8089/view/test/job/run_test_no_env_rebuild/)|Run system tests without creating a new set of VMs|
|[create_env](http://max-tst-01.mariadb.com:8089/view/env/job/create_env/)|Create VMs, install build environment to Maxscale machine, build Maxscale on Maxscale machine|
|[destroy](http://max-tst-01.mariadb.com:8089/view/axilary/job/destroy/)|Destroy VMs created by [run_test](http://max-tst-01.mariadb.com:8089/view/test/job/run_test/) or [create_env](http://max-tst-01.mariadb.com:8089/view/env/job/create_env/)|
|[remove_lock](http://max-tst-01.mariadb.com:8089/view/axilary/job/remove_lock/)|Remove Vagrant lock set by [run_test](http://max-tst-01.mariadb.com:8089/view/test/job/run_test/) or [create_env](http://max-tst-01.mariadb.com:8089/view/env/job/create_env/)|

Every test run should have unique name (parameter 'name'). This name is used as a name of MDBCI configuration. 
If parameter 'do_not_destroy' is set to 'yes' virtual machines (VM) are not destroyed after tests execution and can be laters used 
for debugging or new test runs (see [run_test_no_env_rebuild](http://max-tst-01.mariadb.com:8089/view/test/job/run_test_no_env_rebuild/))
VMs can be accessed from vagrant@max-tst-01.mariadb.com machine using 'mdbci ssh' or 'vagrant ssh' as well as direct ssh 
access using environmental variables provided by 
[set_env_vagrant.sh](https://github.com/mariadb-corporation/maxscale-system-test/blob/master/ENV_SETUP.md#access-vms) 
script.

Parameter 'box' defines type of VM and Linux distribution to be used for tests.

Test results go to [CDash](http://jenkins.engskysql.com/CDash/index.php?project=MaxScale), logs and core dumps are 
stored [here](http://max-tst-01.mariadb.com/LOGS/).

[create_env](http://max-tst-01.mariadb.com:8089/view/env/job/create_env/) job allows to create a set of VMs 
(for backend and Maxscale) and does Maxscale build on the Maxscale VM. After execution this job Maxscale machine 
contains Maxscale source and binaries. *NOTE:* to properly configure Maxscale init scripts it is necessary to 
use rpm/dpkg tool to install Maxscale package (package can be found in the Maxscale build directory).

[run_test](http://max-tst-01.mariadb.com:8089/view/test/job/run_test/) and
[create_env](http://max-tst-01.mariadb.com:8089/view/env/job/create_env/)
jobs create Vagrant lock which prevents running two Vagrant instances in parallel (such parallel execution can 
cause Vagrant of VM provider failures). In case of job crash or interruption by user Vagrant lock stays in locked state 
and prevents any new VM creation. To remove lock job
[remove_lock](http://max-tst-01.mariadb.com:8089/view/axilary/job/remove_lock/) 
should be used.
