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
    cp -r -t $PWD/ $src/maxgui/
fi

cd $PWD/maxgui

export buildPath=$PWD
export VUE_APP_GIT_COMMIT=$(cd $src && git rev-list --max-count=1 HEAD)
npm ci --production
npm run build
