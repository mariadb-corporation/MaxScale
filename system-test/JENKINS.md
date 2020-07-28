# Jenkins

## List of Jenkins installations

| URL | Description |
|----|----|
|[max-tst-01.mariadb.com:8089](http://max-tst-01.mariadb.com:8089)|AWS, qemu; Regular testing for different MariaDB versions, different Linux distributions, Developers testing|
|[maxscale-jenkins.mariadb.com:8089/](http://maxscale-jenkins.mariadb.com:8089/)|AWS, VBox; Regular builds for all distributions, build for Coverity, regular test VBox+CentOS6+MariaDB5.5|
|[maxscale-jenkins.mariadb.com:8090](http://maxscale-jenkins.mariadb.com:8090/)|MDBCI testing and debugging, Jenkins experiments|

## Basic Jenkins jobs

### [max-tst-01.mariadb.com:8089](http://max-tst-01.mariadb.com:8089)

| Job | Description |
|----|----|
|[build_and_test](http://max-tst-01.mariadb.com:8089/view/test/job/build_and_test/)|Build Maxscale and run systems tests|
|[run_test](http://max-tst-01.mariadb.com:8089/view/test/job/run_test/)|Run system tests, Maxscale package should be in the repository|
|[build](http://max-tst-01.mariadb.com:8089/job/build/build)|Build Maxscale, create repository and publish it to [http://max-tst-01.mariadb.com/ci-repository/](http://max-tst-01.mariadb.com/ci-repository/)|
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

## Process examples

### Running regression test against a branch

Execute [build_and_test](http://max-tst-01.mariadb.com:8089/view/test/job/build_and_test/)

Recommendations regarding parameters:

* 'name' - unique name: it can be any text string, but as a good practice rule: 'name' should refer to branch,
Linux distribution, date/time of testing, MariaDB version
* 'box' - most recommended boxes are 'centos_7.0_libvirt' (QEMU box) and 'centos7' (Amazon Web Services box)
* 'source' - which type of source to use. BRANCH for git branch, TAG for a git tag and COMMIT for a commit ID.
* 'value' - name of the branch (if 'source' is BRANCH), name of the GIT tag (if 'source' is TAG) or commint ID (if 'source' is COMMIT)

### Build MaxScale

Execute [build](http://max-tst-01.mariadb.com:8089/job/build/build) job.

Parameter 'target' means a name of repository to put packages:
e.g. if 'target' is 'develop' packages are going to
[http://max-tst-01.mariadb.com/ci-repository/develop/](http://max-tst-01.mariadb.com/ci-repository/develop)

NOTE: building is executed only for selected distribution ('box' parameter). Be careful with  other distributions: if build is not executed for same distribution old version can be in the repository (from some old builds). Later tests have to be executed against the same distribution otherwise they can be run against old version of MaxScale. It is recommended to use unique name for 'target'.

To debug failed build:
* set 'do_not_destroy_vm' parameter to 'yes'
* after the build:
<pre>
ssh -i vagrant.pem vagrant@max-tst-01.mariadb.com
cd ~/mdbci/build-&lt;box&gt;-&lt;date&gt;&lt;time&gt;
vagrant ssh
</pre>

For example:
<pre>
ssh -i vagrant.pem vagrant@max-tst-01.mariadb.com
cd ~/mdbci/build_centos6-20160119-0935
vagrant ssh
</pre>

### Create set of Master/Slave and Galera nodes and setup build environment for Maxscale on one more node

Execute [create_env](http://max-tst-01.mariadb.com:8089/view/env/job/create_env/) job.

Login to Maxscale machine (see [environment documentation](ENV_SETUP.md#access-vms)).
MaxScale source code, binaries and packages can be found in the ~/workspace/ directory.
All build tools are installed. GIT can be used to go trough source code.
It is not recommended to commit anything from virtual machine to GitHub.

Please use 'rpm' or 'dpkg' to properly install Maxscale package (/etc/init.d/maxscale script will not be
installed without execution of 'rpm' or 'dpkg')

### Running test agains exiting version of Maxscale

Execute [run_test](http://max-tst-01.mariadb.com:8089/view/test/job/run_test/) job.

Be sure Maxscale binary repository is present on the
[http://max-tst-01.mariadb.com/ci-repository/](http://max-tst-01.mariadb.com/ci-repository/)
server. Please check:
* there is a directory with the name equal to 'target' parameter
* there is sub-directory for selected distribution ('box' parameter)

e.g. if 'target' is 'develop' and distribution is CentOS7 (boxes 'centos7' or 'centos_7.0_libvirt') the directory [http://max-tst-01.mariadb.com/ci-repository/develop/mariadb-maxscale/centos/7/x86_64/](http://max-tst-01.mariadb.com/ci-repository/develop/mariadb-maxscale/centos/7/x86_64/) have to contain Maxscale RPM packages.

If parameter 'do_not_destroy' set to 'yes' after the test virtual machine will not be destroyed and
can be used for debugging. See [environment documentation](ENV_SETUP.md#access-vms) to get know how to access virtual machines.

### Maintenance operations

If test run was executed with parameter 'do_not_destroy' set yo 'yes' please do not forget to execute
[destroy](http://max-tst-01.mariadb.com:8089/view/axilary/job/destroy/) against your 'target'

This job also have to be executed if test run job crashed or it was interrupted.
