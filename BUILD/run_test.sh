cd ~/Maxscale/maxscale-system-test

cmake .
make

ctest -LE HEAVY -VV
