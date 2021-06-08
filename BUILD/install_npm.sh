#!/bin/bash

function is_arm() {
    [ "$(arch)" == "aarch64" ]
}

if is_arm
then
    node_arch=arm64
else
    node_arch=x64
fi

# NodeJS
node_version=14.17.0
wget --quiet https://nodejs.org/dist/v${node_version}/node-v${node_version}-linux-${node_arch}.tar.gz
tar -axf node-v${node_version}-linux-${node_arch}.tar.gz
sudo cp -t /usr -r node-v${node_version}-linux-${node_arch}/*
