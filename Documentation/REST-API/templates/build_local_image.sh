#!/bin/bash

#
# This is a helper script that builds a local docker image that can be
# used to generate the REST-API documentation.
#

scriptdir=$(dirname $(realpath $0))
cd "$scriptdir/../../../"
docker build -t maxscale-rest-api -f "$scriptdir/Dockerfile" .
cd "$scriptdir"
