#!/bin/bash

if [ "$(arch)" == "aarch64" ]
then
    node_arch=arm64
elif [ "$(arch)" == "ppc64le" ]
then
    node_arch=ppc64le
else
    node_arch=x64
fi

# Node version can be passed as an argument
NODE_MAJOR_VERSION=${1:-16}

if command -v node > /dev/null && [ $(node --version|grep -o -P '(?<=v)[0-9]*') -ge $NODE_MAJOR_VERSION ]
then
    echo "Found Node.js $(node --version), which is recent enough."
    exit 0
fi

node_version=$(curl -s -L https://nodejs.org/download/release/latest-v${NODE_MAJOR_VERSION}.x/SHASUMS256.txt |grep -E "node-v[0-9.]+-linux-x64.tar.gz"|cut -f 2 -d '-')
echo "Installing Node.js $node_version"
wget --quiet https://nodejs.org/download/release/latest-v${NODE_MAJOR_VERSION}.x/node-${node_version}-linux-${node_arch}.tar.gz
tar -axf node-${node_version}-linux-${node_arch}.tar.gz
cp -t /usr -r node-${node_version}-linux-${node_arch}/*
