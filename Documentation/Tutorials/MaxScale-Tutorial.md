# Setting up MariaDB MaxScale

This document is designed as a quick introduction to setting up MariaDB MaxScale.

The installation and configuration of the MariaDB Server will not be covered in
this document. The [Setting Up Replication](https://mariadb.com/kb/en/mariadb/setting-up-replication/)
article on the MariaDB knowledgebase can help you get started with replication clusters
and the
[Getting Started With Mariadb Galera Cluster](https://mariadb.com/kb/en/mariadb/getting-started-with-mariadb-galera-cluster/)
article will help you set up a Galera cluster.

This tutorial will assume the user is running from one of the binary distributions
available and has installed this in the default location.
Building from source code in GitHub is covered in the
[Building from Source](../Getting-Started/Building-MaxScale-from-Source-Code.md) document.

## Installing MaxScale

The precise installation process will vary from one distribution to another.
Details of what to do with the RPM and DEB packages
[can be found on the MaxScale download page](https://mariadb.com/downloads/mariadb-tx/maxscale)
when you select the distribution you are downloading from.

## Creating Database Users

After installation, we need to create a database user. We do this as we need to
connect to the backend databases to retrieve the user authentication
information. To create this user, execute the following SQL commands.

```
CREATE USER 'maxscale'@'%' IDENTIFIED BY 'maxscale_pw';
GRANT SELECT ON mysql.user TO 'maxscale'@'%';
GRANT SELECT ON mysql.db TO 'maxscale'@'%';
GRANT SELECT ON mysql.tables_priv TO 'maxscale'@'%';
GRANT SELECT ON mysql.roles_mapping TO 'maxscale'@'%';
GRANT SHOW DATABASES ON *.* TO 'maxscale'@'%';
```

These credentials will be used by the services in MaxScale to populate the user
authentication data. The tutorials that follow will be using these credentials.

## Creating additional grants for users

**Note:** The client host and MaxScale host must have the same username and
  password for both client and MaxScale hosts.

Because MariaDB MaxScale sits between the clients and the backend databases, the
backend databases will see all clients as if they were connecting from MariaDB
MaxScale's address. This usually means that you must create two sets of grants
for each user.

For example, if you have the `'jdoe'@'client-host'` user and MaxScale is located
at `maxscale-host`, the `'jdoe'@'maxscale-host'` user must be created with the
same password as `'jdoe'@'client-host'` and given the same grants that
`'jdoe'@'client-host'` has.

The quickest way to do this is to first create the new user:

```
CREATE USER 'jdoe'@'maxscale-host' IDENTIFIED BY 'my_secret_password';
```

Then do a `SHOW GRANTS` query:

```
MariaDB [(none)]> SHOW GRANTS FOR 'jdoe'@'client-host';
+-----------------------------------------------------------------------+
| Grants for jdoe@client-host                                           |
+-----------------------------------------------------------------------+
| GRANT SELECT, INSERT, UPDATE, DELETE ON *.* TO 'jdoe'@'client-host'   |
+-----------------------------------------------------------------------+
1 row in set (0.01 sec)
```

Followed by copying grant the same grants to the `'jdoe'@'maxscale-host'` user.

```
GRANT SELECT, INSERT, UPDATE, DELETE ON *.* TO 'jdoe'@'maxscale-host';
```

Another other option is to use a wildcard grant that covers both hosts.  This is
more convenient but less secure than having specific grants for both the
client's hostname and MariaDB MaxScale's hostname as it allows access from all
hosts.

## Creating the configuration file

The next step is to create the configuration file. This part is covered in two
different tutorials.

For a fully automated read/write splitting setup, read the
[Read Write Splitting Tutorial](Read-Write-Splitting-Tutorial.md).
For a simpler connection based setup, read the
[Connection Routing Tutorial](Connection-Routing-Tutorial.md).
