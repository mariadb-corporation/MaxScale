#!/bin/bash

# Create a temporary directory: /tmp/tmp.<random>
SECRETS_DIR=`mktemp -d`

# Generate a .secrets file.
../maxkeys -u $(whoami) ${SECRETS_DIR} || exit 1

# Generate a key.
KEY1=$(../maxpasswd ${SECRETS_DIR} dummy)
KEY2=$(../maxpasswd ${SECRETS_DIR} dummy)

# Remove the temporary directory.
rm -rf ${SECRETS_DIR}

# Get the length of the generated KEY.
LENGTH=${#KEY1}

if [ ${LENGTH} -eq 32 ]
then
    # Exactly 32 characters => Ok!

    if [ "$KEY1" == "$KEY2" ]
    then
        # Password encryption returns the same results when invoked multiple times
        exit 0
    else
        echo "First generated key ($KEY1) and second generated key ($KEY2) do not match."
        exit 1
    fi
else
    # Something else; chances are maxpasswd outputs garbage.
    echo "Expected 32 characters, got $LENGTH"
    exit 1
fi
