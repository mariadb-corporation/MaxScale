#!/bin/bash

set -e

sudo systemctl start vault-dev.service

# Give it a few seconds to start up: the API takes a few seconds to start up.
sleep 2

export VAULT_TOKEN=$(cat ~/.vault-token)
export VAULT_ADDR='http://127.0.0.1:8200'

# Use the root token in the maxscale.cnf. Not quite what you'd do in production
# but it's adequately for testing.
sudo sed -i "s/vault[.]token.*/vault.token=$VAULT_TOKEN/" /etc/maxscale.cnf

# Put the first version of the key into Vault. MaxScale needs it to exist when
# starting up.
openssl rand -hex 32|vault kv put secret/1 data=-
