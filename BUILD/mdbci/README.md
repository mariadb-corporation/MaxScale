# Building Maxscale on Virtual Machine with MDBCI

MDBCI is a tool to manage virtual machines (VM). VM can be described in
the simple JSON format.

libvirt/qemu and AWS VMs are supported.

See [MDBCI README](https://github.com/mariadb-corporation/mdbci#mariadb-continuous-integration-infrastructure-mdbci) for details.

Installation instructions: [PREPARATION_FOR_MDBCI](https://github.com/mariadb-corporation/mdbci/blob/integration/PREPARATION_FOR_MDBCI.md)

## Build script

BUILD/mdbci/build.sh prepares VM, executes Maxscale build and creates binary RPM or DEB repository.

Build options can be defined in the environmental variables. For variables descriptions and default values see 
comments ib the BUILD/mdbci/build.sh and BUILD/mdbci/set_build_variables.sh

## Default build

build.sh can be executed without defining any variable.

Prerequirements:
* MDBCI installed
* Vagrant and vagrant-libvirt plugin installed
* $HOME/maxscale_gpg_keys/ directory contains one public and one corresponding private key (files with .public and .private extensions)
* $HOME/team_keys file contains public keys to be installed to VM

By default VM will stay alive after the build. It have to be deleted manually (with `vagrant destroy` command)

VM will be created in $HOME/vms directory, binary repository will be created the in $HOME/repository/ directory.
The name of binary repository sub-directory is generated based on source default branch name and current date and time.

## Upgrade test

If `run_upgrade_test` variable set to `yes` the upgrade test will be executed after the build.
Upgrade test includes installation of old version of Maxscale from production repository 
(https://downloads.mariadb.com/MaxScale/) and upgrading it to recently built version.

The old version can be defined in `old_target` variable.

Upgrade test starts Maxscale with only one service - `CLI` and tries to execute 'maxadmin' command.
If this command exits with success ('0') upgrade test reports PASSED.

## Build with AWS VM

To build using AWS VM it is necessary to configure AWS credentials and install `vagrant-aws` plugin.
AWS credentials have to be configured in $HOME/.aws directory as described in the 
[AWS CLI documentation](https://docs.aws.amazon.com/cli/latest/userguide/cli-chap-welcome.html)


