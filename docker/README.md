# MariaDB MaxScale Docker image

This Docker image runs the latest GA version of MariaDB MaxScale.

## Building

Run the following command in this directory to build the image.

```
docker build -t maxscale .
```

## Usage

You must mount your configuration file into `/etc/maxscale.cnf.d/`. To do
this, pass it as an argument to the `-v` option:

```
docker run -v $PWD/my-maxscale.cnf:/etc/maxscale.cnf.d/my-maxscale.cnf maxscale:latest
```

By default, MaxScale runs with the `-l stdout` arguments. To explicitly
define a configuration file, use the `-f /path/to/maxscale.cnf` argument
and add `-l stdout` after it.

```
docker run --network host --rm -v /my_dir:/container_dir maxscale -f /path/to/maxscale.cnf -l stdout
```

## Default configuration

The default configuration for the MaxScale docker image can be found in
[this configuration file](./maxscale.cnf).

## MaxScale docker-compose setup

[The MaxScale docker-compose setup](./docker-compose.yml) contains MaxScale
configured with a three node master-slave cluster. To start it, run the
following commands in this directory.

```
docker-compose build
docker-compose up -d
```

After MaxScale and the servers have started (takes a few minutes), you can find
the readwritesplit router on port 4006 and the readconnroute on port 4008. The
user `maxuser` with the password `maxpwd` can be used to test the cluster.

You can edit the [`maxscale.cnf.d/example.cnf`](./maxscale.cnf.d/example.cnf)
file and recreate the MaxScale container to change the configuration.

To stop the containers, execute the following command. Optionally, use the -v
flag to also remove the volumes.

```
docker-compose down
```
