#!/bin/bash

if [ $# -ne 2 ]
then
    echo "USAGE: <path to boost source tarball> <boost version>"
    exit 1
fi

src=$1
version=$2
output_dir=$(tar -taf $src |head -n 1|cut -f 1 -d '/')

tar -axf $src || exit 1

(
    # Remove all files that aren't source files or are part of a library that isn't header-only
    cd $output_dir || (echo "No such directory: $output_dir" && exit 1)
    rm -r *.jam  *.css *.png *.bat *.sh *.htm *.html \
       doc INSTALL Jamroot libs more README.md status tools
)

mv $output_dir boost
tar -caf boost-$version.tar.gz boost
rm -r boost
