Building pp_mysqlembedded
=========================

- Clone the MariaDB server.

- Build the server.

cd server
mkdir build
cd build
cmake .. -DCMAKE_BUILD_TYPE=RelWithDebInfo -DWITH_EMBEDDED_SERVER=1 -DWITH_UNIT_TESTS=0
make

- When configuring MaxScale

cmake .. -DMARIADB_SERVER_SOURCE_DIR=.../server

The build directory is *assumed* to be ${MARIADB_SERVER_SOURCE_DIR}/build.
If something else is used, then MARIADB_SERVER_BUILD_DIR can be used for
specifying that.

At install time, by default,
${MARIADB_SERVER_BUILD_DIR}/sql/share/english/errmsg.sys will be installed.
That can be overridden using MARIADB_ERRMSG.
