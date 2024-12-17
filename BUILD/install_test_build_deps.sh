#!/bin/bash

scriptdir=$(dirname $(realpath $0))

# Install all build dependences
${scriptdir}/install_build_deps.sh

function install_mariadb_repo() {
    # A few system tests need mariadb-test which is not available in all OS repositories
    curl -LsS https://r.mariadb.com/downloads/mariadb_repo_setup | sudo bash -s -- --mariadb-server-version=10.11 --skip_maxscale --skip-check-installed
}

if command -v apt-get
then
    # Debian or Ubuntu
    export DEBIAN_FRONTEND=noninteractive
    apt_cmd="sudo -E apt-get -q -o Dpkg::Options::=--force-confold \
       -o Dpkg::Options::=--force-confdef -y"
    ${apt_cmd} install curl oathtool php-cli php-mysql openjdk-17-jdk krb5-user maven unixodbc-dev
    install_mariadb_repo
    ${apt_cmd} install mariadb-test mariadb-plugin-gssapi-client
elif command -v dnf
then
    # RHEL, Rocky Linux or Alma Linux

    # This enables the CodeReadyBuilder on RHEL/Rocky 9 which is needed by
    # some packages in the EPEL repository.
    sudo dnf install -y dnf-plugins-core
    sudo dnf config-manager --enable crb
    sudo dnf install -y epel-release

    # The --allowerasing is needed on systems where curl-minimal is installed instead of curl.
    sudo dnf install -y --allowerasing curl php-cli php-mysqlnd oathtool java-17-openjdk maven-openjdk17 krb5-workstation  unixODBC-devel
    install_mariadb_repo
    sudo dnf install -y MariaDB-test MariaDB-client
else
    # This is something we don't support running tests on (e.g. SLES)
    echo "ERROR: Cannot run system tests on this OS."
    cat /etc/os-release
    exit 1
fi

# The tests need a very new CMake in order to produce JUnit XML output. This
# ends up installing CMake twice which isn't nice. The install_build_deps.sh
# script could be modified to take the CMake version as an argument which could
# then be passed down to install_cmake.sh.
${scriptdir}/install_cmake.sh "3.25.1"
