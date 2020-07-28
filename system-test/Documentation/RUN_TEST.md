# How to run test

## Prerequirements 

Installed [MDBCI](https://github.com/mariadb-corporation/mdbci) (with dependencies, see 
[MDBCI doc](https://github.com/mariadb-corporation/mdbci#mariadb-continuous-integration-infrastructure-mdbci)), 
[build-scripts](https://github.com/mariadb-corporation/build-scripts-vagrant)

Componets should be in the following directories:

[build-scripts](https://github.com/mariadb-corporation/build-scripts-vagrant) - in ~/build-scripts/

[mdbci](https://github.com/mariadb-corporation/mdbci) - in ~/mdbci/

## Creating test environment and running tests

[run_test.sh](test/run_test.sh) generates MDBCI description of configuration, bring all VMs up, setup DB on all backends,
prapare DB for creating Master/Slave and Galera setups, build [maxscale-system-test](https://github.com/mariadb-corporation/MaxScale/tree/develop#maxscale-system-test)
package, execute ctest. Source code of 
[maxscale-system-test](https://github.com/mariadb-corporation/MaxScale/tree/develop/maxscale-system-test#maxscale-system-test)
have to be in current directory before execution [run_test.sh](test/run_test.sh)

Environmental variables have to be defined before executing [run_test.sh](test/run_test.sh)
For details see [description](README.md#run_testsh)

Example:
<pre>
export export MDBCI_VM_PATH=$HOME/vms
export name="my-centos7-release-1.3.0-test"
export box="centos7"
export product="mariadb"
export version="5.5"
export target="develop"
export ci_url="http://max-tst-01.mariadb.com/ci-repository/"
export do_not_destroy_vm="yes"
export test_set="1,10,,20,30,95"
~/build-scripts/test/run_test.sh
</pre>

After the test, all machines can be accessed:
<pre>
cd $MDBCI_VM_PATH/$name
vagrant ssh \<machine_name\>
</pre>

where \<machine_name\> is 'maxscale', 'node0', ..., 'node3', ..., 'nodeN', 'galera0', ..., 'galera3', ..., 'galeraN'

http://max-tst-01.mariadb.com/ci-repository/develop/mariadb-maxscale/ have to contain Maxscale repository

## Running tests with existing test environment

[set_env_vagrant.sh](test/set_env_vagrant.sh) script sets all needed environmental variables for 
[maxscale-system-test](https://github.com/mariadb-corporation/MaxScale/tree/develop/maxscale-system-test)

See [maxscale-system-test documentation](https://github.com/mariadb-corporation/MaxScale/tree/develop/maxscale-system-test#environmental-variables) for details regarding variables.

Example:
<pre>
export name="running_conf_name"
. ../build-scripts/test/set_env_vagrant.sh $name
set +x
git clone https://github.com/mariadb-corporation/maxscale.git
cd MaxScale/maxscale-system-test
cmake .
make
./test_executable_name
</pre>

or use ctest to run several tests

## Creating environment for Maxscale debugging 

[create_env.sh](test/create_env.sh) script generates MDBCI description of configuration, bring all VMs up,
setup DB on all backends, prapare DB for creating Master/Slave and Galera setups, copy source code of
[Maxscale](https://github.com/mariadb-corporation/MaxScale) to 'maxscale' VM and build it.

Note: script does not install Maxscale, it have to be done manually.

Following variables have to be defined:

'name', 'box', 'product', 'version' 
(see [run_test.sh documentation](https://github.com/mariadb-corporation/build-scripts-vagrant/blob/master/README.md#run_testsh))

'source', 'value' 
(see 
[prepare_and_build.sh documentation](https://github.com/mariadb-corporation/build-scripts-vagrant/blob/master/README.md#prepare_and_buildsh))

Example:
<pre>
export MDBCI_VM_PATH=$HOME/vms
export name="my-centos7-release-1.3.0-test"
export box="centos7"
export product="mariadb"
export version="5.5"
export source="BRANCH"
export value="develop"
~/build-scripts/test/create_env.sh
</pre>

**Note**: do not forget to destroy test environment by vagrant destroy:

<pre>
cd $MDBCI_VM_PATH/$name/
vagrant destroy -f 
</pre>
