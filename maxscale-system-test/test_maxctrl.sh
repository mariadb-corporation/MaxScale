#!/bin/bash

# Check branch name
ref=$(maxscale --version-full 2>&1|grep -o ' - .*'|sed 's/ - //')

if [ -z "$ref" ]
then
    echo "Error: No commit ID in --version-full output"
    exit 1
fi

if [ ! -d MaxScale ]
then
    git clone https://www.github.com/mariadb-corporation/MaxScale.git
fi

cd MaxScale
git checkout $ref
cd maxctrl

cat <<EOF > start_maxscale.sh
#!/bin/sh
sudo systemctl start maxscale
EOF

cat <<EOF >start_double_maxscale.sh
#!/bin/sh
exit 1
EOF

cat <<EOF >stop_maxscale.sh
#!/bin/sh

sudo systemctl stop maxscale

sudo rm -rf /var/lib/maxscale/*
sudo rm -rf /var/cache/maxscale/*
sudo rm -rf /var/run/maxscale/*

if [ -f /tmp/maxadmin.sock ]
then
    sudo rm /tmp/maxadmin.sock
fi

EOF

cat <<EOF >stop_double_maxscale.sh
#!/bin/sh

sudo systemctl stop maxscale

sudo rm -rf /var/lib/maxscale/*
sudo rm -rf /var/cache/maxscale/*
sudo rm -rf /var/run/maxscale/*

if [ -f /tmp/maxadmin.sock ]
then
    sudo rm /tmp/maxadmin.sock
fi

EOF

chmod +x *.sh
npm i

# Export the value for --basedir where maxscale binaries are located
export MAXSCALE_DIR=/usr
./stop_maxscale.sh

npm test
