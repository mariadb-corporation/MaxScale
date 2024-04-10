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
