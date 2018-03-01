# Installing MariaDB MaxScale using a tarball

MariaDB MaxScale is also made available as a tarball, which is named like
`maxscale-x.y.z.OS.tar.gz` where `x.y.z` is the same as the corresponding version and `OS`
identifies the operating system, e.g. `maxscale-2.0.1.centos.7.tar.gz`.

In order to use the tarball, the following libraries are required:

- libcurl
- libaio
- OpenSSL

The tarball has been built with the assumption that it will be installed in `/usr/local`.
However, it is possible to install it in any directory, but in that case MariaDB MaxScale
must be invoked with a flag.

## Installing as root in `/usr/local`

If you have root access to the system you probably want to install MariaDB MaxScale under
the user and group `maxscale`.

The required steps are as follows:

    $ sudo groupadd maxscale
    $ sudo useradd -g maxscale maxscale
    $ cd /usr/local
    $ sudo tar -xzvf maxscale-x.y.z.OS.tar.gz
    $ sudo ln -s maxscale-x.y.z.OS maxscale
    $ cd maxscale
    $ sudo chown -R maxscale var

Creating the symbolic link is necessary, since MariaDB MaxScale has been built
with the assumption that the plugin directory is `/usr/local/maxscale/lib/maxscale`.

The symbolic link also makes it easy to switch between different versions of
MariaDB MaxScale that have been installed side by side in `/usr/local`;
just make the symbolic link point to another installation.

In addition, the first time you install MariaDB MaxScale from a tarball
you need to create the following directories:

    $ sudo mkdir /var/log/maxscale
    $ sudo mkdir /var/lib/maxscale
    $ sudo mkdir /var/run/maxscale
    $ sudo mkdir /var/cache/maxscale

and make `maxscale` the owner of them:

    $ sudo chown maxscale /var/log/maxscale
    $ sudo chown maxscale /var/lib/maxscale
    $ sudo chown maxscale /var/run/maxscale
    $ sudo chown maxscale /var/cache/maxscale

The following step is to create the MariaDB MaxScale configuration file `/etc/maxscale.cnf`.
The file `etc/maxscale.cnf.template` can be used as a base.
Please refer to [Configuration Guide](Configuration-Guide.md) for details.

When the configuration file has been created, MariaDB MaxScale can be started.

    $ sudo bin/maxscale --user=maxscale -d

The `-d` flag causes maxscale _not_ to turn itself into a daemon,
which is adviseable the first time MariaDB MaxScale is started, as it makes it easier to spot problems.

If you want to place the configuration file somewhere else but in `/etc`
you can invoke MariaDB MaxScale with the `--config` flag,
for instance, `--config=/usr/local/maxscale/etc/maxscale.cnf`.

Note also that if you want to keep _everything_ under `/usr/local/maxscale`
you can invoke MariaDB MaxScale using the flag `--basedir`.

    $ sudo bin/maxscale --user=maxscale --basedir=/usr/local/maxscale -d

That will cause MariaDB MaxScale to look for its configuration file in
`/usr/local/maxscale/etc` and to store all runtime files under `/usr/local/maxscale/var`.

## Installing in any Directory

Enter a directory where you have the right to create a subdirectory. Then do as follows.

    $ tar -xzvf maxscale-x.y.z.OS.tar.gz

The next step is to create the MaxScale configuration file `maxscale-x.y.z/etc/maxscale.cnf`.
The file `maxscale-x.y.z/etc/maxscale.cnf.template` can be used as a base.
Please refer to [Configuration Guide](Configuration-Guide.md) for details.

When the configuration file has been created, MariaDB MaxScale can be started.

    $ cd maxscale-x.y.z.OS
    $ bin/maxscale -d --basedir=.

With the flag `--basedir`, MariaDB MaxScale is told where the `lib`, `etc` and `var`
directories are found. Unless it is specified, MariaDB MaxScale assumes
the `lib` directory is found in `/usr/local/maxscale`,
and the `var` and `etc` directories in `/`.

It is also possible to specify the directories and the location of
the configuration file individually. Invoke MaxScale like

    $ bin/maxscale --help

to find out the appropriate flags.
