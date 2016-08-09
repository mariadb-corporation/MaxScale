#
# Copyright (c) 2016 MariaDB Corporation Ab
#
# Use of this software is governed by the Business Source License included
# in the LICENSE.TXT file and at www.mariadb.com/bsl.
#
# Change Date: 2019-01-01
#
# On the date above, in accordance with the Business Source License, use
# of this software will be governed by version 2 or later of the General
# Public License.
#

#!/bin/bash

# Check that all links to Markdown format files point to existing local files

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
