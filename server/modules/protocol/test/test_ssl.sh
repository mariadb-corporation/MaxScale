#!/usr/bin/env bash

function create_certs()
{
    echo "CA cert" > @CMAKE_CURRENT_BINARY_DIR@/ca.pem
    echo "Server Certificate" > @CMAKE_CURRENT_BINARY_DIR@/server-cert.pem
    echo "Server Key" > @CMAKE_CURRENT_BINARY_DIR@/server-key.pem
}

function start_maxscale ()
{
    local result=$(@CMAKE_INSTALL_PREFIX@/@MAXSCALE_BINDIR@/maxscale -d -f $1 &> $1.log;echo $?)
    if [[ $result == "0" ]]
    then
        echo "Error: $1 exited with status $result!"
        exit 1
    fi
}

# All test cases expect that MaxScale will not start with a bad configuration or missing certificates

# No CA defined
printf "Testing No CA defined"
start_maxscale @CMAKE_CURRENT_BINARY_DIR@/no_ca.cnf
echo " OK"

# No cert defined
printf "Testing No cert defined"
start_maxscale @CMAKE_CURRENT_BINARY_DIR@/no_cert.cnf
echo " OK"

# No key defined
printf "Testing No key defined"
start_maxscale @CMAKE_CURRENT_BINARY_DIR@/no_key.cnf
echo " OK"

# Bad SSL value defined
printf "Testing Bad SSL defined"
start_maxscale @CMAKE_CURRENT_BINARY_DIR@/bad_ssl.cnf
echo " OK"

# Bad SSL version defined
printf "Testing Bad SSL version defined"
start_maxscale @CMAKE_CURRENT_BINARY_DIR@/bad_ssl_version.cnf
echo " OK"

# Bad CA value defined
printf "Testing Bad CA defined"
start_maxscale @CMAKE_CURRENT_BINARY_DIR@/bad_ca.cnf
echo " OK"

# Bad server certificate defined
printf "Testing Bad cert defined"
start_maxscale @CMAKE_CURRENT_BINARY_DIR@/bad_cert.cnf
echo " OK"

# Bad server key defined
printf "Testing Bad key defined"
start_maxscale @CMAKE_CURRENT_BINARY_DIR@/bad_key.cnf
echo " OK"

# No CA file
printf "Testing No CA file"
create_certs
rm @CMAKE_CURRENT_BINARY_DIR@/ca.pem
start_maxscale @CMAKE_CURRENT_BINARY_DIR@/ok.cnf
echo " OK"

# No server certificate file
printf "Testing No cert file"
create_certs
rm @CMAKE_CURRENT_BINARY_DIR@/server-cert.pem
start_maxscale @CMAKE_CURRENT_BINARY_DIR@/ok.cnf
echo " OK"

# No server key file
printf "Testing No key file"
create_certs
rm @CMAKE_CURRENT_BINARY_DIR@/server-key.pem
start_maxscale @CMAKE_CURRENT_BINARY_DIR@/ok.cnf
echo " OK"

exit 0
