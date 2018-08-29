# Running Maxscale system tests on Virtual Machines with MDBCI

MDBCI is a tool to manage virtual machines (VMs).
VMs can be described in the simple JSON format.
JSON templates for test configuration can be found in
[maxscale-system-test/mdbci/templates/](templates/)

'MDBCI_VM_PATH' have to be set before executing any MDBCI commands.
This variable points to the directory to store 'Vagrantfile's
for all VMs as well as all additional files (e.g. *network_config)

libvirt/qemu and AWS VMs are supported.

See [MDBCI README](https://github.com/mariadb-corporation/mdbci#mariadb-continuous-integration-infrastructure-mdbci) for details.

Installation instructions: [PREPARATION_FOR_MDBCI](https://github.com/mariadb-corporation/mdbci/blob/integration/PREPARATION_FOR_MDBCI.md)

## Basics of test setup

Test setup is described in template. Templates are stored in
[maxscale-system-test/mdbci/templates/](templates/)
Own template have to be put to the same directory.

Default environment for tests consists of:
* 2 VMs for Maxscales
* 4 VMs for master/slave setup
* 4 VMs for Galera cluster

Template for this configuration is
[maxscale-system-test/mdbci/templates/default.json.template](templates/default.json.template)

Another templates:

Template name|Description
---|---
 ```nogalera``` |only 1 VM for Maxscale and 4 for Master/Slaves|
 ```twomaxscales``` |2 VMs for Maxscale and 4 for Master/Slaves|
 ```onemaxscale``` |1 VM for Maxscale, 4 for Master/Slaves and 4 for Galera|
 ```big``` |1 VM for Maxscale, 8 for Master/Slaves and 4 for Galera|
 ```big15``` |1 VM for Maxscale, 15 for Master/Slaves and 4 for Galera|

```box``` for ```big``` and ```big15``` is hard-coded as ```centos_7_aws_large```

Template can contain references to any environmental variables - they all
will be replaced with values before VMs starting

The [maxscale-system-test/mdbci/run_test.sh](run_test.sh) script
brings test VMs configuration up and tries to execute
```maxscale-system-test``` using 'ctest'.

Script can be executed without any parameters and without defining any
environmental variables.
In this case, tests will be executed for CentOS 7, MariaDB 10.2 and
Maxscale from current 'develop' repository
[http://max-tst-01.mariadb.com/ci-repository/develop/mariadb-maxscale/](http://max-tst-01.mariadb.com/ci-repository/develop/mariadb-maxscale/)
VMs will not be destroyed after the tests.

The name of test run (and name of VMs set) is generated based on ```box``` parameter
and current date/time

### Basic run_test.sh parameters

Variable name|Meaning
---|---
```target``` |name of binary repository to install Maxscale from|
```box``` |Vagrant box to be used to create VMs |
```test_set``` |Set of test to be executed in the 'ctest' format|
```version```|Version of DB server in Master/Slave backend|
```galera_version```|Version of DB server in Galera backend|
```product```|Type of backend - 'mariadb' or 'mysql'|
```template```|Name of *.json.template file with VMs descriptions in MDBCI format|
```team_keys```|Path to the file with public ssh keys - this file is loaded to VMs|
```do_not_destroy_vm```|if 'yes' VMs stay alive after test|
```name```|The name of test run - any string to identify VMs set|


For complete list of environmental variables see comments in
[maxscale-system-test/mdbci/run_test.sh](run_test.sh)
and file [maxscale-system-test/mdbci/set_run_test_variables.sh](set_run_test_variables.sh)

See [https://github.com/mariadb-corporation/mdbci/tree/integration/BOXES](https://github.com/mariadb-corporation/mdbci/tree/integration/BOXES)
for available boxes.

```test_set``` examples

test_set|Meaning
---|---
```-I 4,4```|Run single test number 4|
```-I 4,4,1,25,30```|Run tests number 4, 25 and 30 (the number '1' is a 'Stride' parameter)|
```-I 1,10```|Run tests from 1 to 10|
```-L REPL_BACKED```|Run all tests with 'REPL_BACKEND' label|
```-LE UNSTABLE```|Run all tests EXCEPT tests with 'UNSTABLE' label|

If ```galera_version``` is not defined the value of ```version``` is used also for Galera backend

### Test execution

After execution of 'run_test.sh` by default VMs stay alive and other tests can be executed.

Test use environmental variables to get all infio about test setup (about VMs).

The script [maxscale-system-test/mdbci/set_env.sh](set_env.sh)
loads all needed values (IPs, paths to ssh keyfiles,
user names, etc) into environmental variables. Script uses
data from ```${MDBCI_VM_PATH}/${name}_network_config``` file
and also calls MDBCI commands.

Script have to be sourced:

```bash
source ./mdbci/set_env.sh $name
```

or

```bash
. ./mdbci/set_env.sh $name
```

After it, any 'maxscale-system-test ' can be executed, e.g.:

```bash
./sql_queries
```

### Basic MDBCI and Vagrant operations

#### Restore ${name}.config_file

```bash
${mdbci_dir}/mdbci show network_config $name
```

#### Suspend VMs

Before rebooting computer it is recommended to suspend
Vagrant-controlled VMs

```bash
cd ${MDBCI_VM_PATH}/$name
vagrant suspend
```

#### Resume suspended VMs

```bash
cd ${MDBCI_VM_PATH}/$name
vagrant resume
```

#### Destroying VMs

```bash
${mdbci_dir}/mdbci destroy $name
```

#### Restoring backend

Every test before any actions checks backend and tries to restore it if its broken.
To restore backend separatelly and for intial backend setup check_backend' can be used:

```bash
. ./mdbci/set_env.sh $name
./check_backend
```

#### Restoring VMs from snapshot

'run_test.sh' makes snapshot of all VMs before tests. The name of snapshot is 'clean'.

In case of problem, after 'snapshot revert' it is recommended to re-create
${name}_network_config file, re-load environmental variables and run
'check_backend'

```bash
${mdbci_dir}/mdbci snapshot revert --path-to-nodes $name --snapshot-name clean
${mdbci_dir}/mdbci show network_config $name
. ./mdbci/set_env.sh $name
./check_backend
```

