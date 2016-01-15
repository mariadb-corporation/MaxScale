# Build and test environment setup and running

### Full build and test setup

<pre>
# install ruby 
sudo apt-get install ruby

# install all needed libraries 
sudo apt-get install libxslt-dev libxml2-dev libvirt-dev zlib1g-dev

# install vagrant
# it is also possible to install Vagrant fro distribution repositoy, but it is recommended to use 1.7.2
wget https://dl.bintray.com/mitchellh/vagrant/vagrant_1.7.2_x86_64.deb
sudo dpkg -i vagrant_1.7.2_x86_64.deb

# install Vagrant plugins
vagrant plugin install vagrant-aws vagrant-libvirt vagrant-mutate

# get MDBCI, build scripts, descriptions of MDBCI boxes and keys from GitHub
git clone https://github.com/OSLL/mdbci.git
git clone git@github.com:mariadb-corporation/mdbci-repository-config.git
git clone git@github.com:mariadb-corporation/build-scripts-vagrant.git
git clone git@github.com:mariadb-corporation/mdbci-boxes

# Copy scripts and boxes to proper places
mv build-scripts-vagrant build-scripts
scp -r mdbci-boxes/* mdbci/

# set proper access rights for ssh keys (for ppc64 machines)
chmod 400 mdbci/KEYS/*

# install all the stuff for test package build
sudo apt-get install cmake libmariadbclient-dev gcc g++ libssl-dev
sudo apt-get install mariadb-client shellcheck

# install qemu (more info https://en.wikibooks.org/wiki/QEMU/Installing_QEMU)
apt-get install qemu qemu-kvm libvirt-bin

# install docker (if needed) - see https://docs.docker.com/engine/installation/

# if cmake from distribution repository is too old it is recommended to build it from latest sources
wget https://cmake.org/files/v3.4/cmake-3.4.1.tar.gz # replace 3.4.1 to latest version
tar xzvf cmake-3.4.1.tar.gz
cd cmake-3.4.1
./bootstrap
make
sudo make install
cd

# sysbench 0.5 should be in sysbench_deb7 directory; it can be built from source:
git clone https://github.com/akopytov/sysbench.git
cd sysbench
./autogen.sh
./configure
make
cd ..
mv sysbench sysbench_deb7

# for OVH servers it is needed to move 'docker' and 'libvirt' working directories to /home 
# (replace 'vagrant' to your home directory name)
cd /var/lib/
sudo mv docker /home/vagrant/
sudo ln -s /home/vagrant/docker docker
cd libvirt
sudo mv images /home/vagrant/
sudo ln -s /home/vagrant/images images
cd

# (HACK) in case of problem with building sysbench:
scp -r vagrant@maxscale-jenkins.mariadb.com:/home/vagrant/sysbench_deb7  .

# (HACK) in case of problem with 'dummy' box:
scp -r vagrant@maxscale-jenkins.mariadb.com:/home/vagrant/.vagrant.d/boxes/dummy .vagrant.d/boxes/

# MariaDBManager-GPG* files are needed for Maxscale builds in the home directory

</pre>

### Setup VMs manually

#### Maxscale and backend machines creation



### Access VMs
