#!/bin/bash

if [ $# -lt 1 ]
then
    echo "Usage: $0 SRC"
    exit 1
fi

src=$1

if [ "$PWD" != "$src" ]
then
    # Copy sources to working directory
    cp -r -t "$PWD/maxctrl" "$src"/maxctrl/*
    cp -r -t "$PWD/" "$src"/VERSION*.cmake
fi

cd "$PWD/maxctrl" || exit 1

if [ "$(arch)" == "aarch64" ]
then
    # Check if we have to build the NodeJS binary from source

    if grep -q -E -i 'stretch|xenial' /etc/os-release
    then
        # GLIBC is too old on Debian Stretch and Ubuntu Xenial
        opts="--build"
    fi

    if grep -q '7' /etc/redhat-release
    then
        # CentOS 7 also has an older GLIBC
        opts="--build"
    fi
fi

# Piping the output through `tee` works around a problem in npm where it always
# prints verbose output: https://github.com/npm/cli/issues/3314
npm install --production |& tee
npm install --production pkg@5 |& tee
node_modules/pkg/lib-es5/bin.js --options max_old_space_size=4096 $opts -t node10-linux . |& tee
