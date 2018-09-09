#!/bin/bash

for file in $(eval "list-src -x 'qc_sqlite pcre2 build'")
do
    uncrustify --no-backup --replace --mtime $file
done

