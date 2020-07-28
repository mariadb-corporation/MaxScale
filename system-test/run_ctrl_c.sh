#!/bin/bash

###
## @file run_ctrl_c.sh
## check that Maxscale is reacting correctly on ctrc+c signal and termination does not take ages
set -x
scp -i ${maxscale_000_keyfile} -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no -r $src_dir/test_ctrl_c/* ${maxscale_000_whoami}@${maxscale_000_network}:./
ssh -i ${maxscale_000_keyfile} -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no ${maxscale_000_whoami}@${maxscale_000_network} "export maxscale_000_access_sudo=${maxscale_000_access_sudo}; ./test_ctrl_c.sh"
res=$?

ssh -i ${maxscale_000_keyfile} -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no ${maxscale_000_whoami}@${maxscale_000_network} "sudo rm -f /tmp/maxadmin.sock"

exit $res
