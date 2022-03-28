#!/bin/bash

currdir=$(dirname $(realpath $0))

$currdir/list-src \
    -i "include maxutils server system-test" \
    -x "inih" \
    $currdir/.. | xargs uncrustify -c $currdir/../uncrustify.cfg --no-backup --mtime
