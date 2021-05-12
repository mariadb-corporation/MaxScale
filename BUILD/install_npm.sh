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
wget --quiet https://nodejs.org/dist/v10.20.0/node-v10.20.0-linux-${node_arch}.tar.gz
tar -axf node-v10.20.0-linux-${node_arch}.tar.gz
sudo cp -t /usr -r node-v10.20.0-linux-${node_arch}/*
