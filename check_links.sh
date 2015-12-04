#!/bin/bash

homedir=$(pwd)

for file in $(find . -name '*.md')
do
    cd "$(dirname $file)"
    for i in `grep -o '\[.*\]([^#].*[.]md)' $(basename $file)| sed -e 's/\[.*\](\(.*\))/\1/'`
    do
        if [ ! -f $i ]
        then
            echo "Link $i in $file is not correct!"
        fi
    done
    cd "$homedir"
done
