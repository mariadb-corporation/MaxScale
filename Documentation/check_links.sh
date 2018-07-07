#!/bin/bash
#
# Copyright (c) 2016 MariaDB Corporation Ab
#
# Use of this software is governed by the Business Source License included
# in the LICENSE.TXT file and at www.mariadb.com/bsl11.
#
# Change Date: 2020-01-01
#
# On the date above, in accordance with the Business Source License, use
# of this software will be governed by version 2 or later of the General
# Public License.
#

# Check that all links to Markdown format files point to existing local files

homedir=$(pwd)

function check_file() {
    file=$1
    grep -o '\[.*\]([^#].*[.]md)' "$(basename "$file")"| sed -e 's/\[.*\](\(.*\))/\1/'|while read i
    do
        if [ ! -f "$i" ]
        then
            echo "Link $i in $file is not correct!"
        fi
    done
}

find . -name '*.md'|while read file
do
    cd "$(dirname "$file")"
    check_file $file
    cd "$homedir"
done

cd ..
check_file $PWD/README.md
cd "$homedir"
