#/bin/sh -f

# these environment variables are set in .travis.yml
# MARIADB_URL=https://downloads.mariadb.org/interstitial/mariadb-5.5.48/bintar-linux-glibc_214-x86_64/mariadb-5.5.48-linux-glibc_214-x86_64.tar.gz/from/http%3A//mirror.netinch.com/pub/mariadb/
# MARIADB_TAR=mariadb-5.5.48-linux-glibc_214-x86_64.tar.gz
# MARIADB_DIR=mariadb-5.5.48-linux-x86_64

# get mariadb
wget --content-disposition ${MARIADB_URL}

# unompress
tar -axf ${MARIADB_TAR}
