#!/bin/bash

ITEMS=`node maxctrl.js help|awk '/^$/{p=0} {if(p){print $1}}/Commands:/{p=1}'`

TOC=$(for i in $ITEMS
do
    echo "* [$i](#$i)"
done)

COMMANDS=$(for i in $ITEMS
do
    echo "## $i"
    echo
    echo "\`\`\`"
    node maxctrl.js help $i|awk 'BEGIN{p=1} /Global Options:/{p=0}{if(p){print $0}}'
    echo "\`\`\`"
    echo

    CMD=`node maxctrl.js help $i|awk '/^$/{p=0} {if(p){print $1}}/Commands:/{p=1}'`

    for j in $CMD
    do
        echo "### $i $j"
        echo
        USAGE=`node maxctrl.js help $i $j|head -n 1`
        echo "Usage: \`$USAGE\`"
        echo ""

        # Print the detailed command explanation if it has one
        DESC=`node maxctrl.js help $i $j|sed 's/[\`]/\\\`/'|awk 'BEGIN{p=2} /Options:/{p=0}{if(p==1){print $0}}/^$/{if(!p){p=1}}'`
        if [ ! -z "$DESC" ]
        then
            echo "$DESC"
            echo
        fi

    done
done)

GLOBALOPTS=$(node maxctrl.js help $i|awk '{if(p){print $0}} /Global Options:/{p=1}')

cat <<EOF > ../Documentation/Reference/MaxCtrl.md
# MaxCtrl

MaxCtrl is a command line administrative client for MaxScale which uses
the MaxScale REST API for communication. It is intended to be the
replacement software for the legacy MaxAdmin command line client.

By default, the MaxScale REST API listens on port 8989 on the local host. The
default credentials for the REST API are \`admin:mariadb\`. The users used by the
REST API are the same that are used by the MaxAdmin network interface. This
means that any users created for the MaxAdmin network interface should work with
the MaxScale REST API and MaxCtrl.

For more information about the MaxScale REST API, refer to the
[REST API documentation](../REST-API/API.md) and the
[Configuration Guide](../Getting-Started/Configuration-Guide.md).

# Commands

$TOC

## Options

All command accept the following global options.

\`\`\`
$GLOBALOPTS
\`\`\`

$COMMANDS

EOF
