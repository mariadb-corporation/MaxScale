# MaxScale HA with Lsyncd

This document guides you in setting up multiple MaxScale instances and synchronizing the configuration files with Lsyncd. Lsyncd is a rsync wrapper which can synchronize files across the network. The lsyncd daemon uses a configuration file to control the files to synchronize and the remote targets where these files are synchronized to.

Copying the configuration file and running the lsyncd daemon on all the hosts keeps all the configuration files in sync. Modifications in the configuration file on one of the hosts will be copied on the other hosts. This allows adinistrators to easily provide a highly available, disaster resistant MaxScale installation with up-to-date configuration files on all the hosts.


## Installing Lsyncd

You can install lsyncd with either a package manager or by building from source code. This guide demonstrates installation using a package manager and those looking to build lsyncd from source should refer to its documentation: https://github.com/axkibe/lsyncd/wiki/Manual-to-Lsyncd-2.1.x

Installing with Yum:

```
yum install lsyncd
```

Installing with Apt:

```
apt-get install lsyncd
```

Lsyncd needs no further configuration after installation. All necessary options are configured in the configuration file passed to the lsyncd daemon.

## Creating the Lsyncd configuration file

Lsyncd uses a configuration file to determine where to read files from and where to synchronize them if changes in them occur. Lsyncd is written in Lua and the configuration file is also valid Lua code.

Here is an example configuration file with descriptions on the meaning of the values in it.

```
-- Lsyncd will log to these two files.
settings{
         logfile         = "/var/log/maxscale/maxscale-ha.log",
         statusFile      = "/var/log/maxscale/maxscale-ha-status.log"
}

-- Copy and paste the sync section and change the host value to add new remote targets.
sync{
default.rsyncssh,

-- This is where the maxscale.cnf file is copied from.
source="/etc", 

-- This is the user and host where the maxscale.cnf is copied to.
host="user@127.0.0.1", 

-- This is where the maxscale.cnf is copied to on the remote host.
targetdir="/etc", 

-- This is an optional section which defines a custom SSH port. Uncomment to enable.
-- ssh={port=2222}, 

-- These are values passed to rsync. Only change these if you know what you are doing.
rsync={
      compress=true,
	  _extra = {[[--filter=+ *maxscale.cnf]],
                [[--filter=- **]]
               }
      }
}
```

The most important part is the `sync` section which defines a target for synchronization. The `default.rsyncssh` tells lsyncd to synchronize files using SSH.

The `source` parameter tells lsyncd where to read the files from. This should be the location of the maxscale.cnf file. The `host` parameter defines the host where the files should be synchronized to and the user account lsyncd should use when synchronizing the files. The `targetdir` parameter defines the local directory on the remote target where the files should be synchronized to. This value should be the location on the remote host where the maxscale.cnf file is searched from. By default, this is the `/etc` folder.

The optional `ssh` parameter and its sub-parameter `port`define a custom port for the SSH connection. Most users do not need this parameterer. The `rsycn` parameter contains an arra of options that are passed to the rsycn executable. These should not be changed unless you specifically know what you are doing. For more information on the options passed to rsync read the rsync(1) manpage.

You can add multiple remote targets by defining multiple `sync` sections. Here is an example with two sync sections defining different hosts that have MaxScale installed and whose configuration files should be kep in sync.

```
settings{
         logfile         = "/var/log/maxscale/maxscale-ha.log",
         statusFile      = "/var/log/maxscale/maxscale-ha-status.log"
}

sync{
default.rsyncssh,
source="/etc", 
host="maxuser@192.168.0.50", 
targetdir="/etc", 
rsync={
      compress=true,
	  _extra = {[[--filter=+ *maxscale.cnf]],
                [[--filter=- **]]
               }
      }
}


sync{
default.rsyncssh,
source="/etc", 
host="syncuser@192.168.122.105", 
targetdir="/etc", 
rsync={
      compress=true,
	  _extra = {[[--filter=+ *maxscale.cnf]],
                [[--filter=- **]]
               }
      }
}
```

## Starting Lsyncd

Starting lsyncd can be done from the command line or through a init script. To start syncd from the command like, execute the `lsyncd` command and pass the configuration file as the only parameter.

Here is an example which start lsyncd and reads the configuration options from the `lsyncd.cnf` file.

```
lsyncd lsyncd.cnf
```

For more information on the lsyncd executable and its options, please see the --help output of lsyncd or the lsyncd(1) manpage.
