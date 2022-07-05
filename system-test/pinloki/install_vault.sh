#!/bin/bash

# Check if vault is already installed.
command -v vault && exit 0

set -e

. /etc/os-release

function install_rpm {
    sudo yum install -y yum-utils
    sudo yum-config-manager --add-repo https://rpm.releases.hashicorp.com/RHEL/hashicorp.repo
    sudo yum -y install vault
}

function install_deb {
    curl -fsSL https://apt.releases.hashicorp.com/gpg | sudo apt-key add -
    sudo apt-add-repository "deb [arch=amd64] https://apt.releases.hashicorp.com $(lsb_release -cs) main"
    sudo apt-get update && sudo apt-get install vault
}

case $ID in
    rhel)
        install_rpm
        ;;
    centos)
        install_rpm
        ;;
    rocky)
        install_rpm
        ;;
    debian)
        install_deb
        ;;
    ubuntu)
        install_deb
        ;;
    *)
        echo "Cannot install Vault for $ID"
        exit 1
        ;;
esac

mkdir -p $HOME/.config/systemd/user/
cat <<EOF > $HOME/.config/systemd/user/vault-dev.service
[Unit]
Description="Vault dev server"

[Service]
ExecStart=$(command -v vault) server -dev

[Install]
WantedBy=default.target
EOF
