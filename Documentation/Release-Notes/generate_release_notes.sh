#!/bin/bash

major=`cmake -P ../../VERSION22.cmake -L|grep 'MAXSCALE_VERSION_MAJOR'|sed 's/.*=//'`
minor=`cmake -P ../../VERSION22.cmake -L|grep 'MAXSCALE_VERSION_MINOR'|sed 's/.*=//'`
patch=`cmake -P ../../VERSION22.cmake -L|grep 'MAXSCALE_VERSION_PATCH'|sed 's/.*=//'`
maturity=`cmake -P ../../VERSION22.cmake -L|grep 'MAXSCALE_MATURITY'|sed 's/.*=//'`

VERSION=${major}.${minor}.${patch}

cat <<EOF > MaxScale-$VERSION-Release-Notes.md
# MariaDB MaxScale ${VERSION} Release Notes

Release ${VERSION} is a ${maturity} release.

This document describes the changes in release ${VERSION}, when compared to the
previous release in the same series.

For any problems you encounter, please consider submitting a bug
report on [our Jira](https://jira.mariadb.org/projects/MXS).

`../list_fixed.sh ${VERSION}`

## Known Issues and Limitations

There are some limitations and known issues within this version of MaxScale.
For more information, please refer to the [Limitations](../About/Limitations.md) document.

## Packaging

RPM and Debian packages are provided for supported the Linux distributions.

Packages can be downloaded [here](https://mariadb.com/downloads/mariadb-tx/maxscale).

## Source Code

The source code of MaxScale is tagged at GitHub with a tag, which is identical
with the version of MaxScale. For instance, the tag of version X.Y.Z of MaxScale
is \`maxscale-X.Y.Z\`. Further, the default branch is always the latest GA version
of MaxScale.

The source code is available [here](https://github.com/mariadb-corporation/MaxScale).
EOF
