# Maxscale Continuous Integration and Test Automation

## Basics

Jenkins at http://max-tst-01.mariadb.com:8089/
is the main tool for providing build and test services for Maxscale,
but note that it _cannot_ be accessed directly (see further down).

A set of virtual machines (VMs) is in use for all building and testing.
The VMs are controlled by [Vagrant](https://www.vagrantup.com/)
and [MDBCI](https://github.com/OSLL/mdbci/).

3 types of VMs are supported:
* Libvirt/qemu
* AWS
* Docker containers

All Jenkins jobs are in
[Jenkins job Builder](https://docs.openstack.org/infra/jenkins-job-builder/)
YAML format and are available
[here](https://github.com/mariadb-corporation/maxscale-jenkins-jobs).

VMs for every build or test run are created from clean Vagrant box.

To speed up regular builds and tests there are a set of constantly running VMs:
* Build machine _centos\_7\_libvirt\_build_
* Test VMs set _centos\_7\_libvirt-mariadb-10.0-permanent_

Build and test Jenkins jobs generate e-mails with results report.
In addition, test run logs are stored [here](http://max-tst-01.mariadb.com/LOGS/).

## Accessing _max-tst-01_

Jenkins running on _max-tst-01_ can only be accessed from localhost. In order
to be able to access it from your computer, an ssh tunnel needs to be setup.

* Ensure that your public key is available on _max-tst-01_.
* Then setup the ssh tunnel:
```
$ ssh -f -N -L 8089:127.0.0.1:8089 vagrant@max-tst-01.mariadb.com
```
After that, Jenkins on _max-tst-01_ can be accessed by browsing to
http://localhost:8089.

In the following it is assumed that the above has been done.

## Build

* [Build](http://localhost:8089/job/build/) builds Maxscale
  and creates a binary repository for one Linux distribution.

* [Build_all](http://localhost:8089/job/build_all/) builds
  Maxscale and creates binary repository for _all_ supported Linux
  distributions.

The list of supported distributions (Vagrant boxes) is
[here](https://github.com/mariadb-corporation/maxscale-jenkins-jobs/blob/master/maxscale_jobs/include/boxes.yaml).

### Main _Build_ and _Build_all_ jobs parameters

To start a build, click on _Build with Parameters_ to the left in
the Jenkins panel.

#### scm_source
The place in Maxscale source code.
* for branch - put branch name here
* for commit ID - put Git Commit ID here
* for tag - put `refs/tags/<tagName>` here

#### box
The name of Vagrant box to create VM.
Box name consists of the name of distribution, distribution version and the name of VM provider.
Whole list of supported boxes can be found [here](https://github.com/mariadb-corporation/maxscale-jenkins-jobs/blob/master/maxscale_jobs/include/boxes_all.yaml)

#### target
Then name of binary repository. Ensure that the name is reasonably
unique so that there can be no unintended overwrites. If you are
building a personal branch, then using the same value for _target_
as is the value for _scm_source_ should be fine.

Binaries will go to:
`http://max-tst-01.mariadb.com/ci-repository/<target>/mariadb-maxscale/<distribution_name>/<version>`

#### cmake_flags

Debug build:
```
-DBUILD_TESTS=Y -DCMAKE_BUILD_TYPE=Debug -DFAKE_CODE=Y -DBUILD_MMMON=Y -DBUILD_AVRO=Y -DBUILD_CDC=Y
```

Release build:
```
-DBUILD_TESTS=N -DFAKE_CODE=N -DBUILD_MMMON=Y -DBUILD_AVRO=Y -DBUILD_CDC=Y
```

Build scripts automatically add all other necessary flags (e.g. -DPACKAGE=Y)

#### do_not_destroy_vm

If `yes`, then the VM will not be destroyed after the build. Choose that **if** you,
for some specific reason (e.g. debugging), intend to log into the VM later.

#### repo_path

The location for binary repositories on _max-tst-01_. Usually no need to change.

#### try_alrady_running
If `yes` constantly running VM will be used for build instead of bringing clean
machines from scratch. The name of VM is `<box_name>_build`.

If this VM is not running it will be created from clean box. If new build
dependency is introduced and new package have to be installed (and build
script is modified to do it) before the build it is necessary to destroy
constantly running VM - in this case a new VM will be created and all stuff
will be installed.

#### run_upgrade_test

If `yes` upgrade test will be executed after build.

Upgrade test is executed on a separate VM.

* First, old version of Maxsale is installed (parameter **old_target**),
* then upgrade to recently built version is executed,
* then Maxscale is started with minimum configuration (only CLI service is
  configured, no backend at all), and
* finally, _maxadmin_ is executed to check that Maxscale is running and available.

### Regular automatic builds

File `~/jobs_branches/build_all_branches.list` on _max-tst-02.mariadb.com contains
a list of regexes. If branch name matches one of the regex from this file
_build_all_ job will be executed for this branch every night.

Regular builds are executed with _run\_upgrade\_test=yes_

## Test execution

* [run_test](http://localhost:8089/view/test/job/run_test/) creates a set of
  VMs: 1 VM for Maxscale, 4 VMs for Master/Slave setup and VMs for Galera cluster.

### Main _Run_test_ parameters

Before this job can be run, a
[build](#build)
job must have been executed first.

#### target
The name of Maxscale binary repository.

The value used here, should be the same as the value used in [target](#target).

#### box
The name of Vagrant box to create VM.

The value used here, must be the same as the value used in
[box](#box)
when the used **target** was built.

#### product
MariaDB or MySQL to be installed to all backend machines.

#### version
Version of MariaDB or MySQL to be installed on all MariaDB machines.
The list of versions contains all MariaDB and MySQL major version.
Selecting wrong version (e.g. 5.7 for MariaDB or 10.0 for MySQL) causes
VM creation error and job failure.

#### do_not_destroy_vm
If `yes` all VMs will run until manually destoroy by **Destroy** job.
It can be used for debugging. **Do not forget to destroy** VMs after debugging
is finished.

#### name
The name of test run. The names of the VMs will be:

* `<name>_maxscale`,
* `<name>_node_000`, ..., `<name>_node_003` and
* `<name>_galera_000`, ..., `<name>_galera_003`-

The **name** can be used to access VMs:

```bash
. ~/build-scripts/test/set_env_vagrant.sh <name>
ssh -i $<vm_name>_keyfile $<vm_name>_whoami@$<vm_name>_network
```
where `<vm_name>` can be 'maxscale', 'node_XXX' or 'galera_XXX'.

#### test_branch
The name of _MaxScale_ repository branch to be used in the test run.

It is usually the same as the one used in [scm_source](#scm_source).

#### slave_name
Host server to for VMs and _maxscale-system-test_ execution

|Name|Server|
|-------|:---------------------|
|master |localhost|
|maxtst2|max-tst-02.mariadb.com|
|maxtst3|max-tst-03.mariadb.com|

#### test_set
Defines tests to be run. See ctest documentation
https://cmake.org/cmake/help/v3.7/manual/ctest.1.html
for details.

Most common cases:

|Arguments|Description|
|-------------|:--------------------------------------------------|
|-I 1,5,,45,77| Execute tests from 1 to 5 and tests 45 and 77|
|-L HEAVY|Execute all tests with 'HEAVY' label|
|-LE UNSTABLE|Execute all tests except tests with 'UNSTABLE' label|


### Run_test_snapshot

This job uses already running VMs. The set of VMs have to have snapshot
(by default snapshot name is 'clean').
Instead of bringing up all VMs from scratch this job only reverts VMs to
the 'clean' state.

The name of running VMs set should be:

`<box>-<product>-<version>-permanent`

If there is no VMs set with such name it will be created automatically.

If case of broken VMs it can be destroyd with help of **Destroy** job
and **Run_test_snapshot** should be restarted after it to create a new
VMs set.

Only one VMs set on every server can be started for particular **box**,
**product** and **version** combination.
If two **Run_test_snapshot** jobs are runnig for the same
**box**, **product** and **version**
the second one will be waiting until the first job run ios finished.
(job sets 'snapshot_lock').

If case if locak is not removed automatically (for example
due to Jenkins crash) it can be removed manually:

```bash
rm ~/mdbci/centos_7_libvirt-mariadb-10.0-permanent_snapshot_lock
```

### Run_test_labels and Run_test_snapshot_labels

The only difference from **Run_test** and
**Run_test_snapshot** is the way to define set of tests to execute.
**\*_labels** jobs use checkboxes list of test labels.

Labels list have to be maintained manually.
It can be created by
https://github.com/mariadb-corporation/maxscale-jenkins-jobs/blob/master/create_labels_jobs.sh
script.

### Regular test runs

#### Test runs by timer

File
```
~/jobs_branches/run_test_branches.list
```
on max-tst-02.mariadb.com
contains a list of regexes. If branch name matches one of regexes
tests are executed for this branch every day.

The test set is defined also in this file.

Job **print_branches_which_matches_regex_in_file**
http://localhost:8089/view/All/job/print\_branches\_which\_matches\_regex\_in\_file/build
can be used to check the list of branches that match
regexes.


Job **weekly_smoke_run_test_matrix** triggers **build_regular**
and **run_test_matrix**
http://localhost:8089/view/test/job/run_test_matrix/
every week for 'develop' branch
with test set '-L LIGHT' (all tests with label 'LIGHT')


#### Test runs by GIT push
File
```
~/jobs_branches/on_push_maxscale_branches.list
```
on max-tst-02.mariadb.com
contains a list of regexes. If branch name matches one of regexes
tests are executed for this branch after every push.

Job **print_branches_which_matches_regex_in_file**
http://localhost:8089/view/All/job/print\_branches\_which\_matches\_regex\_in\_file/build
can be used to check the list of branches that match
regexes (select _on\_push\_maxscale\_branches.list_
file for _branches\_list_ parameter)


## Debugging

For regular debugging contsntly running set of VMs is recommended.
See [documentation here](DEBUG_ENVIRONMENT.md).

Another way is to use **do_not_destroy=yes** parameter of **run_test** job.
After **run_test** job executing VMs stay running and
can be accessed from the host server. See [here](LOCAL_DEPLOYMENT.md#accessing-vms)
for details.


