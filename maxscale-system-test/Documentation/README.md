# build-scripts-vagrant

Build and test scripts to work with Vagrant-controlled VMs do follwoing:
* create VM for Maxscale build
* create a set of VMs (test environment) for running Maxscale tests
 
Test environment consists of:
* 'maxscale' machine
* 'nodeN' machines for Master/Slave setup (node0, node1, node2, node3)
* 'galeraN' machines for Galera cluster (galera0, galera1, galera2, galera3)

## Main files

File|Description
----|-----------
[prepare_and_build.sh](prepare_and_build.sh)|Create VM for build and execute build, publish resulting repository
build.\<provider\>.template.json|templates of MDBCI configuration description (build environment description) of build machines|
[test-setup-scripts/setup_repl.sh](test-setup-scripts/setup_repl.sh)|Prepares repl_XXX machines to be configured into Master/Slave
[test-setup-scripts/galera/setup_galera.sh](test-setup-scripts/galera/setup_galera.sh)|Prepares galera_XXX machines to be configured into Galera cluster
[test-setup-scripts/cnf/](test-setup-scripts/cnf/)|my.cnf files for all backend machines
test/template.\<provider\>.json|Templates of MDBCI configuration description (test environment description) of test machines|
[test/run_test.sh](test/run_test.sh)|Creates test environment, build maxscale-system-tests from source and execute tests using ctest
[test/set_env_vagrant.sh](test/set_env_vagrant.sh)|set all environment variables for existing test machines using MDBCI to get all values
[test/create_env.sh](test/create_env.sh)|Creates test environment, copy Maxscale source code to 'maxscale' machine in this environment, build Maxscale

## [prepare_and_build.sh](prepare_and_build.sh)
Following variables have to be defined before executing prepare_and_build.sh

|Variable|Meaning|
|--------|--------|
|$MDBCI_VM_PATH|Path to duirectory to store VM info and Vagrant files (by default $HOME/vms)|
|$box|name of MDBCI box (see [MDBCI docs](https://github.com/OSLL/mdbci#terminology))|
|$target|name of repository to put result of build|
|$scm_source|reference to the place in Maxscale source GIT repo|
|$cmake_flags|additional cmake flags|
|$do_not_destroy_vm|if 'yes' build VM won't be destroyed after the build. NOTE: do not forget destroy it manually|
|$no_repo|if 'yes' repository won't be built|

Scripts creates MDBCI configuration build_$box-<current data and time>.json, bringing it up (the directory build_$box-<current data and time> is created)

Resulting DEB or RPM first go to ~/pre-repo/$target/$box

NOTE: if ~/pre-repo/$target/$box contains older version they will also go to repostory

Resulting repository goes to ~/repository/$target/mariadb-maxscale/

It is recommeneded to publish ~/repository/ directory on a web server

## [test/run_test.sh](test/run_test.sh)
Following variables have to be defined before executing run_test.sh

|Variable|Meaning|
|--------|--------|
|$box|name of MDBCI box for Maxscale machine (see [MDBCI docs](https://github.com/OSLL/mdbci#terminology))|
|$name|unique name of test setup|
|$product|'mariadb' or 'mysql'|
|$version|version of backend DB|
|$target|name of Maxscale repository|
|$ci_url|URL of repostory web site, Maxscale will be installed from $ci_url/$target/mariadb-maxscale/
|$do_not_destroy_vm|if 'yes' build VM won't be destroyed after the build. NOTE: do not forget to destroy it manually|
|$smoke|if 'yes' all tests are executed in 'quick' mode (less iterations, skip heavy operations)|
|$test_set|definition of set of tests to run in ctest notation|
|test_set_labels|list of ctest labels. If it is not define 'test_set' will be used as a plain string|

## Test environment operations

### Accessing nodes
<pre>
cd $MDBCI_VM_PATH/$name/
vagrant ssh $node_name
</pre>

where $node_name - 'maxscale', 'node0', ..., 'node3', ..., 'nodeN', 'galera0', ..., 'galera3', ..., 'galeraN'

### Getting IP address and access keys
<pre>
~/mdbci/mdbci show network $name
~/mdbci/mdbci show network $name/$node_name
~/mdbci/mdbci show keyfile $name/$node_name
</pre>

### Destroying environemnt
<pre>
cd $MDBCI_VM_PATH/$name/
vagrant destroy -f
</pre>

### Set variables by [test/set_env_vagrant.sh](test/set_env_vagrant.sh)
<pre>
. ../build-scripts/test/set_env_vagrant.sh $name
</pre>
