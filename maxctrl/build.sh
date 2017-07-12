#/bin/bash

if [ $# -lt 1 ]
then
    echo "Usage: $0 SRC"
    exit 1
fi

# Copy sources to working directory
src=$1
cp -r -t $PWD/maxctrl $src/maxctrl/* && cd $PWD/maxctrl

npm install
npm install pkg
node_modules/pkg/lib-es5/bin.js -t node6-linux-x64 .
