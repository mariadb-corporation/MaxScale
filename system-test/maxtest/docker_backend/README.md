# MariaDB Server Docker image for MaxScale system tests

This Docker image runs MariaDB Server as a service which can be started and
stopped without affecting the container itself. The image also runs the sshd
server in case SSH access is required. The image must be built before running
system tests against Docker backends.

## Building

Run the following command in the directory. The image tag must match the
image name given in the test configuration file.
```
docker build -f Dockerfile --tag mxs_test_server:1 .
```

## Running

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

## Running a system test

The Docker backends are used in local test mode. This means that MaxScale is
running locally and the servers in Docker containers. Because everything is
running on the local machine, some test features (such as blocking)
are not available. Local mode requires a test config file which replaces the
MDBCI network_config file. The test config file format is similar to a typical
MaxScale config file but contains different elements.

See the [example config](example_docker_test.cnf) for details. Make a copy of
the file and set correct paths to the `start_cmd` and `maxctrl_cmd` lines in
the `[maxscale1]`-section.

To launch a local test, give the test config file as argument to the system
test executable:
```
./client_privileges -l /home/me/local_docker_test.cnf
```
The test will create the backend server containers if they are not yet running.
