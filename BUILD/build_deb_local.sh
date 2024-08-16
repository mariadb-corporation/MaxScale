#!/bin/bash

# do the real building work
# this script is executed on build VM
scriptdir=$(dirname $(realpath $0))

set -x

"$scriptdir"/build_package.sh || exit 1

cd MaxScale/_build/
cp _CPack_Packages/Linux/DEB/*.deb ../

cd ..
cp _build/*.deb .
cp *.deb ..
cp _build/*.gz .

set -x
sudo dpkg -i ../maxscale*.deb
set +x
