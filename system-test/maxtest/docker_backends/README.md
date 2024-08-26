# Running MaxScale system tests against Docker backends

Many system tests can now run against MariaDB Servers and/or MaxScale itself
running in Docker containers, with the containers managed by the
TestConnections-class. This gives a level of automation and isolation somewhat
similar to the virtual machine based test backends without having to install
MDBCI and related VM management tools.

The MaxScale being tested can run either outside (local mode) or inside a
container. Running locally reduces the number of tests supported as e.g. remote
commands are not supported. Servers are best ran in containers due to
limitations of local mode servers.

Running tests against local or Docker nodes requires a test config file which
replaces the MDBCI network_config file (as MDBCI is not used). The test config
file uses the MaxScale config file format. See [below](test-configuration) for
more information.

## MariaDB Server Dockerfile

The [server Dockerfile](server/Dockerfile) runs MariaDB Server as a service
which can be started and stopped without affecting the container itself. The
image also runs the sshd server in case SSH access is required. The image must
be built before running system tests against Docker backends.

Run the following command in the server-directory. The image tag must match the
image name given in the test configuration file.
```
docker build -f Dockerfile --tag mxs_test_server:1 .
```

## MaxScale Dockerfile

This step is not required if you are testing against a locally running MaxScale.
A Docker-based MaxScale is needed if the test runs sudo-level remote commands
on the MaxScale node or copies files to it.

The [MaxScale Dockerfile](maxscale/Dockerfile) works similarly as the server
Dockerfile except that it requires a MaxScale package during image build. To
generate a package, give
`-DCMAKE_INSTALL_PREFIX=/usr/local -DPACKAGE=Y -DWITH_MAXSCALE_CNF=Y -DWITH_SCRIPTS=Y`
to CMake and run `make package`. Not all MaxScale modules need to be built.
Copy the generated .deb-file to the maxscale-directory and change its name to
`maxscale.deb`. Then run
```
docker build -f Dockerfile --tag mxs_test_maxscale:1 .
```
in the directory.

The Dockerfile is based on Ubuntu Jammy and will install compatible MaxScale
packages. The Dockerfile will need to be modified to work on other systems. For
Ubuntu-like distributions changing the first line should suffice.

## Test configuration

System tests require a configuration file which defines what type of backends
are available and how to interact with them. Two example files are available:
one which runs MaxScale locally and one which runs MaxScale inside a container.
MariaDB Servers are inside containers.

To test against a local MaxScale, make a copy of
[local_maxscale.cnf.example](local_maxscale.cnf.example) and set correct paths
to the `start_cmd` and `maxctrl_cmd` lines in the `[maxscale1]`-section.
Everything else in the test configuration file should be usually left to
default values.

To run tests against MaxScale inside a container, make a copy of
[docker_maxscale.cnf.example](docker_maxscale.cnf.example). The file should be
fine as is.

Some tests block and unblock MariaDB Server nodes. When the servers are in
separate virtual machines, this is implemented by modifying iptables inside the
VMs. Docker does not support this, which means that the host iptables need
to be modified instead. To enable tests that depend on node blocking, configure
`iptables_cmd` in the `[common]`-section of the test configuration. To allow
the system tests to run the configured command, create a file into the
directory `etc/sudoers.d` with the contents:

```
my_user ALL= (root) NOPASSWD: /usr/sbin/iptables
```
`my_user` should be the Linux user running the test.
`/usr/sbin/iptables` is the path to the configured iptables command.

## Running a system test

To launch a local test, give the test config file as argument to the system
test executable:
```
./client_privileges -l /home/me/my_test_config.cnf
```
The test will create the backend server containers if they are not yet running.

The test config file can also be given in the environment variable
`local_test_config`:
```
export local_test_config=/home/me/my_test_config.cnf
```
After this, the test can be launched simply with `./client_privileges`.

## Testing the containers

The containers are automatically created and started by the test system when
starting a test. These instructions are meant for testing the images itself.

Start the server container:
```
docker run -d --rm --mount type=volume,source=test_server_data,destination=/var/lib/mysql --name server1 mxs_test_server:1
```
Start/stop the MariaDB Server process inside the container:
```
docker exec server1 /command/s6-svc -u /run/service/mariadbd
docker exec server1 /command/s6-svc -d /run/service/mariadbd
```
Retrieve the container IP address:
`docker inspect server1 | grep \"IPAddress\"`.

Log in to the MariaDB Server: `mariadb --host=<ip> --user=root`.

Log in to the container with SSH: `ssh -i admin_key test-admin@<ip>`.

Start the MaxScale container:
```
docker run -d --rm --name mxs mxs_test_maxscale:1
```
Start/stop the MaxScale process inside the container:
```
docker exec mxs /command/s6-svc -u /run/service/maxscale
docker exec mxs /command/s6-svc -d /run/service/maxscale
```
