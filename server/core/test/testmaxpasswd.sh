#!/bin/bash

# Create a temporary directory: /tmp/tmp.<random>
SECRETS_DIR=`mktemp -d`

# Generate a .secrets file.
../maxkeys ${SECRETS_DIR} || exit 1

# Generate a key.
KEY=`../maxpasswd ${SECRETS_DIR} dummy`

# Remove the temporary directory.
rm -rf ${SECRETS_DIR}

# Get the length of the generated KEY.
LENGTH=${#KEY}

if [ ${LENGTH} -eq 32 ]
then
    # Exactly 32 characters => Ok!
    exit 0
else
    # Something else; chances are maxpasswd outputs garbage.
    exit 1
fi
