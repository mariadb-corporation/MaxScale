#/bin/bash

if [ $# -lt 1 ]
then
    echo "Usage: $0 SRC"
    exit 1
fi

src=$1

if [ "$PWD" != "$src" ]
then
    # Copy sources to working directory
    cp -r -t $PWD/maxctrl $src/maxctrl/*
    cp -r -t $PWD/ $src/VERSION*.cmake
fi

cd $PWD/maxctrl

npm install --production
npm install --production pkg@4.4.5
node_modules/pkg/lib-es5/bin.js -t node10-linux .
