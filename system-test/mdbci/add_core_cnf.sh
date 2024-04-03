#
# Copyright (c) 2016 MariaDB Corporation Ab
# Copyright (c) 2023 MariaDB plc, Finnish Branch
#
# Use of this software is governed by the Business Source License included
# in the LICENSE.TXT file and at www.mariadb.com/bsl11.
#
# Change Date: 2028-04-03
#
# On the date above, in accordance with the Business Source License, use
# of this software will be governed by version 2 or later of the General
# Public License.
#

if [ "$1" == "verbose" ]
then
    set -x
fi

chmod 777 /tmp/
echo 2 > /proc/sys/fs/suid_dumpable
sed -i "s/start() {/start() { \n export DAEMON_COREFILE_LIMIT='unlimited'; ulimit -c unlimited; /" /etc/init.d/maxscale
sed -i "s/log_daemon_msg \"Starting MaxScale\"/export DAEMON_COREFILE_LIMIT='unlimited'; ulimit -c unlimited; log_daemon_msg \"Starting MaxScale\" /" /etc/init.d/maxscale
echo /tmp/core-%e-%s-%u-%g-%p-%t > /proc/sys/kernel/core_pattern

echo "kernel.core_pattern = /tmp/core-%e-sig%s-user%u-group%g-pid%p-time%t" >> /etc/sysctl.d/core.conf
echo "kernel.core_uses_pid = 1" >> /etc/sysctl.d/core.conf
echo "fs.suid_dumpable = 2" >> /etc/sysctl.d/core.conf

echo "DefaultLimitCORE=infinity" >> /etc/systemd/system.conf

echo "*       hard        core        unlimited" >> /etc/security/limits.d/core.conf
echo "*       soft        core        unlimited" >> /etc/security/limits.d/core.conf
echo "*       soft     nofile           65536" >> /etc/security/limits.d/core.conf
echo "*       hard     nofile           65536" >> /etc/security/limits.d/core.conf


echo "fs.file-max = 65536" >>  /etc/sysctl.conf

systemctl daemon-reexec
sysctl -p
