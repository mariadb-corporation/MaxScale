# Running Maxscale ststem tests on Virtual Machines with MDBCI

MDBCI is a tool to manage virtual machines (VM). VM can be described in
the simple JSON format.

libvirt/qemu and AWS VMs are supported.

See [MDBCI README](https://github.com/mariadb-corporation/mdbci#mariadb-continuous-integration-infrastructure-mdbci) for details.

Installation instructions: [PREPARATION_FOR_MDBCI](https://github.com/mariadb-corporation/mdbci/blob/integration/PREPARATION_FOR_MDBCI.md)

## Basics

Default environment for tests consists of:
* one VM for Maxscale
* 4 VMs for master/slave setup
* 4 VMs for Galera cluster

Test setup is discribed in template. Templates are stored in 
[template/](template/)



