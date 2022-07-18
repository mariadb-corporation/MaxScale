#!/bin/bash
set -e

# Check if vault is already installed.
if ! command -v vault
then
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
fi

cat <<EOF > vault-dev.service
[Unit]
Description="Vault dev server"

[Service]
ExecStart=$(command -v vault) server -dev
User=$(whoami)
WorkingDirectory=$HOME

[Install]
WantedBy=default.target
EOF

sudo mv vault-dev.service /etc/systemd/system/
sudo chcon system_u:object_r:systemd_unit_file_t:s0 /etc/systemd/system/vault-dev.service
sudo systemctl daemon-reload
