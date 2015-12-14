#
# This file is distributed as part of the MariaDB Corporation MaxScale.  It is free
# software: you can redistribute it and/or modify it under the terms of the
# GNU General Public License as published by the Free Software Foundation,
# version 2.
#
# This program is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
# FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
# details.
#
# You should have received a copy of the GNU General Public License along with
# this program; if not, write to the Free Software Foundation, Inc., 51
# Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
#
# Copyright MariaDB Corporation Ab 2015
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
