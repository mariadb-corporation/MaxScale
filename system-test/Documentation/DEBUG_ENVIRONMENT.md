# Debug environment

## Create ssh tunnel to Jenkins server

```bash
ssh -f -N -L 8089:127.0.0.1:8089 vagrant@max-tst-01.mariadb.com
```

## Create environment for debugging

To create virtual machines for debugging please 
use Jenkins job 'create_env'
http://127.0.0.1:8089/view/env/job/create_env/build

This Jenkins job creates backend VMs
(4 Master/Slave and 4 Galera) and
Maxscale development machine.

Maxscale development machine will contain all 
build tools and build dependencies as well as
Maxscale source Git.

Source is located in:
```
~/MaxScale/
```

## Environmental variables setup

```bash
. ~/build-scripts/test/set_env_vagrant.sh <name>
```

Example:

```bash
. ~/build-scripts/test/set_env_vagrant.sh debug_env
```

## Access to Maxscale VM

```bash
ssh -i $maxscale_sshkey $maxscale_whoami@maxscale_network
```

```bash
scp -i $maxscale_sshkey <stuff_to_copy> $maxscale_whoami@maxscale_network:/home/$maxscale_whoami/
```

```bash
scp -i $maxscale_sshkey $maxscale_whoami@maxscale_network:/home/$maxscale_whoami/<stuff_to_copy> .
```

## Executing tests

Clone https://github.com/mariadb-corporation/maxscale

and build tests

```bash
cd MaxScale/maxscale-system/test
cmake .
make
```


and then run 

```bash
ctest -VV
```

or manually any test executable from _maxscale-system-test_

It is recommended to run 

```bash
./check_backend
```

before manual testing to be sure Master/Slave and Galera setups are
in order (_check_backend_ also fixes broken replication or Galera)

## Restoring broken setup

Just use http://127.0.0.1:8089/view/snapshot/job/restore_snapshot/build

Manual snapshot reverting:

```bash
~/mdbci/mdbci snapshot  revert --path-to-nodes debug_env --snapshot-name clean
```

## Destroying 

Use http://127.0.0.1:8089/view/axilary/job/destroy/build
with _name=debug_env_

or _clean_vms.sh_ script

```bash
cd ~/mdbci/scripts
./clean_vms.sh debug_env
```

## Notes

Please check _slave_name_ parameter when executing any Jenkins job.
All jobs are executed only for defined slave (or for master).

i.e. VM set with the same name can be running on different slaves at the same time.


