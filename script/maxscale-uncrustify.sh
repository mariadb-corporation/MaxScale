#!/bin/bash

currdir=$(dirname $(realpath $0))

$currdir/list-src -x 'sqlite-src-3110100 pcre2' $currdir/..|xargs uncrustify -c $currdir/../uncrustify.cfg --no-backup --mtime

