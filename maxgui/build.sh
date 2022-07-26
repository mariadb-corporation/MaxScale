#!/bin/bash

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

# Piping the output through `tee` works around a problem in npm where it always
# prints verbose output: https://github.com/npm/cli/issues/3314
export buildPath=$PWD
export VUE_APP_GIT_COMMIT=$(cd $src && git rev-list --max-count=1 HEAD)
npm ci --production --omit=optional |& tee
npm run build |& tee
