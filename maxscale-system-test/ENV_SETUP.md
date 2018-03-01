# Build and test environment setup

### Full build and test environment setup

<pre>
# install ruby
sudo apt-get install ruby

# install all needed libraries
sudo apt-get install libxslt-dev libxml2-dev libvirt-dev zlib1g-dev

# install vagrant
# it is also possible to install Vagrant from distribution repository, but in case of problems please use 1.7.2
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
sudo apt-get install cmake gcc g++ libssl-dev
sudo apt-get install mariadb-client shellcheck

# install MariaDB development library
sudo apt-get install libmariadbclient-dev
# Ubuntu repos can contain the sa,e package with different name 'libmariadb-client-lgpl-dev'
# but it can not be used to build maxscale-system-test; please use mariadb.org repositories
# https://downloads.mariadb.org/mariadb/repositories/
# Do not forget to remove all other MariaDB and MySQL packages!

# install qemu (more info https://en.wikibooks.org/wiki/QEMU/Installing_QEMU)
sudo apt-get install qemu qemu-kvm libvirt-bin

# install virt-manager (if you prefer UI)
sudo apt-get install virt-manager

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

# (HACK) in case of problem with 'dummy' box (problem is caused by MDBCI bug):
scp -r vagrant@maxscale-jenkins.mariadb.com:/home/vagrant/.vagrant.d/boxes/dummy .vagrant.d/boxes/

# MariaDBManager-GPG* files are needed for Maxscale builds in the home directory

# put AWS keys to aws-config.yml (see https://github.com/OSLL/mdbci/blob/master/aws-config.yml.template)

# add curent user to the group 'libvirtd'
sudo usermod -a -G user_name libvirtd

# start libvirt default pool
virsh pool-start default

</pre>

### Setup VMs manually

#### Empty virtual machine

Following template can be used to create empty VM (for qemu machines):
<pre>
{
  "cookbook_path" : "../recipes/cookbooks/",
  "build" :
  {
        "hostname" : "default",
        "box" : "###box###",
        "product" : {
                "name" : "packages"
        }
  }
}
</pre>

for AWS machines:
<pre>
{
  "cookbook_path" : "../recipes/cookbooks/",
  "aws_config" : "../aws-config.yml",
  "build" :
  {
        "hostname" : "build",
        "box" : "###box###"
  }
}
</pre>

Following boxes are availabe:
* qemu: debian_7.5_libvirt, ubuntu_trusty_libvirt, centos_7.0_libvirt, centos_6.5_libvirt
* AWS: rhel5, rhel6, rhel7, sles11, sles12, fedora20, fedora21, fediora22, ubuntu_wily, ubuntu_vivid, centos7, deb_jessie

#### Maxscale and backend machines creation

* Generation of Maxscale repository description
It is necessary to generate descriptions of MariaDB and Maxscale repositories before bringin up Maxscale machine with Vagrant
<pre>
export ci_url="http://my_repository_site.com/repostory/"
~/mdbci-repository-config/generate_all.sh $repo_dir
~/mdbci-repository-config/maxscale-ci.sh $target $repo_dir
</pre>
where
<pre>
$repo_dir - directory where repository descriptions will be created
$target - directory with MaxScale packages in the repositoy
</pre>
example:
<pre>
export ci_url="http://max-tst-01.mariadb.com/ci-repository/"
~/mdbci-repository-config/generate_all.sh repo.d
~/mdbci-repository-config/maxscale-ci.sh develop repo.d
</pre>
More information can be found in the [MDBCI documentation](https://github.com/OSLL/mdbci#repod-files) and in the [mdbci-repository-config documentaion](https://github.com/mariadb-corporation/mdbci-repository-config#mdbci-repository-config)

* Preparing configuration description
Virtual machines should be described in JSON format. Example template can be found in the [build-scripts package](https://github.com/mariadb-corporation/build-scripts-vagrant/blob/master/test/template.libvirt.json).

MariaDB machine description example:
<pre>
"node0" :
  {
        "hostname" : "node0",
        "box" : "centos_7.0_libvirt",
        "product" : {
                "name": "mariadb",
                "version": "10.0",
                "cnf_template" : "server1.cnf",
                "cnf_template_path": "~/build-scripts/test-setup-scripts/cnf"
        }

  }
</pre>

"cnf_template" defines .cnf file which will be places into MariaDB machine. [build-scripts package](https://github.com/mariadb-corporation/build-scripts-vagrant/tree/master/test-setup-scripts/cnf) contains examples of .cnf files.

MariaDB Galera machine description example:
<pre>
"galera0" :
  {
        "hostname" : "galera0",
        "box" : "centos_7.0_libvirt",
        "product" : {
                "name": "galera",
                "version": "10.0",
                "cnf_template" : "galera_server1.cnf",
                "cnf_template_path": "~/build-scripts/test-setup-scripts/cnf"
        }
  }
</pre>

For Galera machines MDBCI automatically puts following information into .cnf file:

|field|description|
|------|----|
|###NODE-ADDRESS###|IP address of the node (for AWS - private IP)|
|###NODE-NAME###|Replaces by node name ("node0" in this example)|
|###GALERA-LIB-PATH###|Path to the Galera library file (.so file)|

Example of Maxscale machine description:
<pre>
"maxscale" :
  {
        "hostname" : "maxscale",
        "box" : "centos_7.0_libvirt",
        "product" : {
                "name": "maxscale"
        }

  }
</pre>

#### Generation configuration and bringing machines up

After creation machines description JSON two steps are needed.

1. Generate configuration
<pre>
./mdbci --override --template $template_name.json --repo-dir $repo_dir generate $name
</pre>

where

|variable|description|
|----|----|
|$template_name|name of machines descripiton JSON file|
|$repo_dir|directory with repositories description generated by mdbci-repository-config (repo.d)|
|$name|name of test configuration; will be used as directory name for Vagrant files|

2. Bringing machines up
<pre>
./mdbci up $name
</pre>

#### Configuring DB users

Automatic DB users is not implemented yet, so it have to be done manually. See [setup_repl.sh](https://github.com/mariadb-corporation/build-scripts-vagrant/blob/master/test-setup-scripts/setup_repl.sh) and [setup_galera.sh](https://github.com/mariadb-corporation/build-scripts-vagrant/blob/master/test-setup-scripts/galera/setup_galera.sh) for details.

Any test from 'maxscale-system-test' checks Master/Slave and Galera configurations and restores them if they are broken, but it works only if DB users are created.

TODO: add it into 'maxscale-system-test'

### Access VMs

MDBCI provides a number of commands to get information about running vrtial machines. See [MDBCI documentation](https://github.com/OSLL/mdbci#mdbci-syntax) for details.

[set_env_vagrant.sh script](https://github.com/mariadb-corporation/build-scripts-vagrant/blob/master/test/set_env_vagrant.sh) defines environmental variables needed by 'maxscale-system-test'. The same variables can be used to access VMs manually.

Script have to be executed fro 'mbdci' directory. Do not forget '.':
<pre>
cd ~/mdbci/
. ../build-scripts/test/set_env_vagrant.sh $name
</pre>

After it virual machines can be accessed via ssh, for example:
<pre>
ssh -i $maxscale_sshkey $maxscale_access_user@$maxscale_IP
</pre>

Another way is to use 'vagrant ssh':
<pre>
cd ~/mdbci/$name/
vagrant ssh &lt;node_name&gt;
</pre>

MDBCI can give IP address, path to ssh key:
<pre>
./mdbci show network &lt;configuration_name&gt;/&lt;node_name&gt; --silent
./mdbci show keyfile &lt;configuration_name&gt;/&lt;node_name&gt; --silent
./mdbci ssh --command 'whoami' &lt;configuration_name&gt;/&lt;node_name&gt; --silent
</pre>

Node name for build machine is 'build'

Nodes names for typical test setup are node0, ..., node3, galera0, ..., galera3, maxscale

Example:
<pre>
./mdbci show network centos6_vm01/build --silent
./mdbci show keyfile centos6_vm01/build --silent
./mdbci ssh --command 'whoami' centos6_vm01/build --silent
</pre>

### Destroying configuration

<pre>
cd ~/mdbci/$name
vagrant destroy -f
</pre>
