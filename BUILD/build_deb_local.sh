#!/bin/bash

# do the real building work
# this script is executed on build VM
scriptdir=$(dirname $(realpath $0))

set -x

"$scriptdir"/build_package.sh || exit 1

mkdir -p MaxScale/_build/
cp _build/*.deb -t MaxScale/
cp _build/*.gz -t MaxScale/_build/
