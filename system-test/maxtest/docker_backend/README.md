# Running MaxScale system tests locally using Docker for MariaDB Server backends

Many system tests can now be run against MariaDB Servers running in Docker
containers, with the containers managed by the TestConnections class. Running
tests this way does require some setup, which this file explains.

## The Dockerfile

The [Dockerfile](Dockerfile) runs MariaDB Server as a service which can be
started and stopped without affecting the container itself. The image also runs
the sshd server in case SSH access is required. The image must be built before
running system tests against Docker backends.

Run the following command in the directory. The image tag must match the
image name given in the test configuration file (explained below).
```
docker build -f Dockerfile --tag mxs_test_server:1 .
```

## Running a system test

The Docker backends are used in local test mode. This means that MaxScale is
running locally and the servers in Docker containers. Because everything is
running on the local machine, some test features (such as running commands
on the MaxScale node) are not available. Local mode requires a test config
file which replaces the MDBCI network_config file (as MDBCI is not used).
The test config file uses the MaxScale config file format.

See the [example config](example_docker_test.cnf) for details. Make a copy of
the file and set correct paths to the `start_cmd` and `maxctrl_cmd` lines in
the `[maxscale1]`-section.

To enable tests that block/unblock nodes, configure `iptables_cmd` in
the `[common]`-section. The example value is likely correct and work in systems
where `iptables -v` returns something containing *nf_tables*. To allow the
system tests to run the configured command, create a file into the directory
`etc/sudoers.d` with the contents:
```
my_user ALL= (root) NOPASSWD: /usr/sbin/iptables-legacy
```
`my_user` should be the Linux user running the test. `/usr/sbin/iptables-legacy`
is the path to the configured iptables command.

To launch a local test, give the test config file as argument to the system
test executable:
```
./client_privileges -l /home/me/local_docker_test.cnf
```
The test will create the backend server containers if they are not yet running.

The test config file can also be given in the environment variable
`local_test_config`:
```
export local_test_config=/home/me/local_docker_test.cnf
```
After this, the test can be launched simply with `./client_privileges`.

## Testing the container

The container is automatically created and started by the test system when
starting a test. These instructions are meant for testing the image itself.

Start the container:
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
