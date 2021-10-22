# Generating the REST API Documentation

## TL;DR

Run the `regenerate.sh` script and add the changes to git with `git add
-p`. Small changes like counters or dates do not always need to be updated to
keep the diffs smaller.

## Steps to Reproduce

* Configure with CMake: `mkdir -p /tmp/build && cd /tmp/build && cmake -DWITH_SCRIPTS=N -DWITH_MAXSCALE_CNF=N -DCMAKE_INSTALL_PREFIX=/tmp/build/ <path to MaxScale sources>`
* Build MaxScale in `/tmp/build/` (this is important: it keeps the paths consistent regardless of who builds it): `cd /tmp/build/ && make install`
* Create required directories: `mkdir -p {cache,run}/maxscale`
* Start a two-node cluster on ports 3000 and 3001, you can use [this docker-compose file](../../../test/docker-compose.yml).
* Start MaxScale using the `rest_api.cnf` that is found at the root of the build directory: `bin/maxscale -df rest_api.cnf`
* Open a connection to MaxScale on port 4006 and execute `SELECT 1`, leave the connection open
* Run `generate.py`
