# MariaDB MaxScale 1.2 Release Notes

## 1.2 GA

This document details the changes in version 1.2 since the release of the 1.1.1 GA Release of the MaxScale product.

###***PLEASE NOTICE: MaxScale installation directories have changed in this version***
The 1.2 version of MaxScale differs from previous versions in its installation layout. Please take great care when upgrading MaxScale from previous versions to version 1.2. An automatic upgrade will not work due to the severe changes in the installation layout.

## New Features

### Non-root MaxScale
You can now run MaxScale as any user. The standard installation of a MaxScale package now creates the maxscale user and the maxscale group.

### FHS-compliant installation
The 1.2 version of MaxScale now complies to the Filesystem Hierarchy Standard. This means that MAXSCALE_HOME is no longer necessary and directories can be moved to different locations.

A quick list of changes in installation directories and file names:

  * Binaries go into `/usr/bin`
  * Configuration files to `/etc` and the configuration file is now lower case: `maxscale.cnf`
  * Logs to `/var/log/maxscale`
  * The module and library directory have been combined into a single directory in `/usr/lib64/maxscale`. If you have custom modules please make sure they are located there.
  * Data directory is `/var/lib/maxscale`. This is the default location for MaxScale-specific data.
  * PID file can be found at `/var/run/maxscale`

### Client side SSL encryption
MaxScale now supports SSL/TLS encrypted connections to MaxScale.

### Launchable scripts
Now you can configure MaxScale monitor module to automatically launch a script when it detects change in the state of a backend server. The script can be any customer script defined by you to take diagnostic or reporting action. With this you can easily customize MaxScale's behavior.

### Lsyncd configuration guide
A new tutorial has been added which helps you keep MaxScale's configuration files in sync across multiple hosts. This allows for easier HA setups with MaxScale and guarantees up-to-date configuration files on all nodes. The tutorial can be found [here](../Reference/MaxScale-HA-with-lsyncd.md).

## Bug fixes

Here is a list of bugs fixed since the release of MaxScale 1.1.1.

 * [MXS-24](https://mariadb.atlassian.net/browse/MXS-24):  bugzillaId-604: Module load path documentation issues ...  
 * [MXS-40](https://mariadb.atlassian.net/browse/MXS-40):  Display logged in users  
 * [MXS-113](https://mariadb.atlassian.net/browse/MXS-113):  MaxScale seems to fail if built against MariaDB 10.0 libraries  
 * [MXS-116](https://mariadb.atlassian.net/browse/MXS-116):  Do not run maxscale as root.  
 * [MXS-117](https://mariadb.atlassian.net/browse/MXS-117):  Allow configuration of the log file directory  
 * [MXS-125](https://mariadb.atlassian.net/browse/MXS-125):  inconsistency in maxkeys/maxpassword output and parameters  
 * [MXS-128](https://mariadb.atlassian.net/browse/MXS-128):  cyclic dependency utils -> log_manager -> utils  
 * [MXS-136](https://mariadb.atlassian.net/browse/MXS-136):  Check for MaxScale replication heartbeat table existence before creating  
 * [MXS-137](https://mariadb.atlassian.net/browse/MXS-137):  cannot get sql for queries with length >= 0x80  
 * [MXS-139](https://mariadb.atlassian.net/browse/MXS-139):  Schemarouter authentication for wildcard grants fails without optimize_wildcard  
 * [MXS-140](https://mariadb.atlassian.net/browse/MXS-140):  strip_db_esc does not work without auth_all_servers  
 * [MXS-162](https://mariadb.atlassian.net/browse/MXS-162):  Fix Incorrect info in Configuration Guide 
 * [MXS-165](https://mariadb.atlassian.net/browse/MXS-165):  Concurrency issue while incrementing sessions in qlafilter  
 * [MXS-166](https://mariadb.atlassian.net/browse/MXS-166):  Memory leak when creating a new event  
 * [MXS-171](https://mariadb.atlassian.net/browse/MXS-171):  Allow reads on master for readwritesplit  
 * [MXS-176](https://mariadb.atlassian.net/browse/MXS-176):  Missing dependencies in documentation  
 * [MXS-179](https://mariadb.atlassian.net/browse/MXS-179):  Keep configuration  changes in synch across MaxScale Mate Nodes  
 * [MXS-180](https://mariadb.atlassian.net/browse/MXS-180):  MariaDB10 binlog router compatibilty 
 * [MXS-181](https://mariadb.atlassian.net/browse/MXS-181):  Poor performance on TCP connection due to Nagle's algoritm  
 * [MXS-182](https://mariadb.atlassian.net/browse/MXS-182):  SHOW SLAVE STATUS and maxadmin "show services" for binlog router needs updated when used with MariaDB 10 Master  
 * [MXS-212](https://mariadb.atlassian.net/browse/MXS-212):  Stopped services accept connections  
 * [MXS-225](https://mariadb.atlassian.net/browse/MXS-225):  RPM Debug build packages have no debugging symbols  
 * [MXS-227](https://mariadb.atlassian.net/browse/MXS-227):  Memory leak in Galera Monitor  
 * [MXS-244](https://mariadb.atlassian.net/browse/MXS-244):  Memory leak when using prepared statements without arguments 

## Known Issues and Limitations

There are a number bugs and known limitations within this version of MaxScale, the most serious of this are listed below.

* MaxScale can not manage authentication that uses wildcard matching in hostnames in the mysql.user table of the backend database. The only wildcards that can be used are in IP address entries.

* When users have different passwords based on the host from which they connect MaxScale is unable to determine which password it should use to connect to the backend database. This results in failed connections and unusable usernames in MaxScale.

* LONGBLOB are currently not supported.

* Galera Cluster variables, such as @@wsrep_node_name, are not resolved by the embedded MariaDB parser.

* The Database Firewall filter does not support multi-statements. Using them will result in an error being sent to the client.   

## Packaging

Both RPM and Debian packages are available for MaxScale in addition to the tar based releases previously distributed we now provide

* CentOS/RedHat 5

* CentOS/RedHat 6

* CentOS/RedHat 7

* Debian 6

* Debian 7

* Ubuntu 12.04 LTS

* Ubuntu 14.04 LTS

* SuSE Linux Enterprise 11

* SuSE Linux Enterprise 12
