/**
 * Minimal MaxCtrl sanity check
 */

#include "testconnections.h"

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);

    int rc = test.maxscales->ssh_node_f(0, false, "maxctrl help list servers");
    test.expect(rc == 0, "`help list servers` should work");

    rc = test.maxscales->ssh_node_f(0, false, "maxctrl --tsv list servers|grep 'Master, Running'");
    test.expect(rc == 0, "`list servers` should return at least one row with: Master, Running");

    rc = test.maxscales->ssh_node_f(0, false, "maxctrl set server server1 maintenance");
    test.expect(rc == 0, "`set server` should work");

    rc = test.maxscales->ssh_node_f(0, false, "maxctrl --tsv list servers|grep 'Maintenance'");
    test.expect(rc == 0, "`list servers` should return at least one row with: Maintanance");

    rc = test.maxscales->ssh_node_f(0, false, "maxctrl clear server server1 maintenance");
    test.expect(rc == 0, "`clear server` should work");

    rc = test.maxscales->ssh_node_f(0, false, "maxctrl --tsv list servers|grep 'Maintenance'");
    test.expect(rc != 0, "`list servers` should have no rows with: Maintanance");

    test.tprintf("Execute all available commands");
    test.maxscales->ssh_node_f(0, false,
                               "maxctrl list servers;"
                               "maxctrl list services;"
                               "maxctrl list listeners RW-Split-Router;"
                               "maxctrl list monitors;"
                               "maxctrl list sessions;"
                               "maxctrl list filters;"
                               "maxctrl list modules;"
                               "maxctrl list threads;"
                               "maxctrl list users;"
                               "maxctrl list commands;"
                               "maxctrl show server server1;"
                               "maxctrl show servers;"
                               "maxctrl show service RW-Split-Router;"
                               "maxctrl show services;"
                               "maxctrl show monitor MySQL-Monitor;"
                               "maxctrl show monitors;"
                               "maxctrl show session 1;"
                               "maxctrl show sessions;"
                               "maxctrl show filter qla;"
                               "maxctrl show filters;"
                               "maxctrl show module readwritesplit;"
                               "maxctrl show modules;"
                               "maxctrl show maxscale;"
                               "maxctrl show thread 1;"
                               "maxctrl show threads;"
                               "maxctrl show logging;"
                               "maxctrl show commands mariadbmon;"
                               "maxctrl drain server server1;"
                               "maxctrl clear server server1 maintenance;"
                               "maxctrl enable log-priority info;"
                               "maxctrl enable account vagrant;"
                               "maxctrl disable log-priority info;"
                               "maxctrl disable account vagrant;"
                               "maxctrl create server server5 127.0.0.1 3306;"
                               "maxctrl create monitor mon1 mariadbmon user=skysql password=skysql;"
                               "maxctrl create service svc1 readwritesplit user=skysql password=skysql;"
                               "maxctrl create filter qla2 qlafilter filebase=/tmp/qla2.log;"
                               "maxctrl create listener svc1 listener1 9999;"
                               "maxctrl create user maxuser maxpwd;"
                               "maxctrl link service svc1 server5;"
                               "maxctrl link monitor mon1 server5;"
                               "maxctrl alter service-filters svc1 qla2"
                               "maxctrl unlink service svc1 server5;"
                               "maxctrl unlink monitor mon1 server5;"
                               "maxctrl alter service-filters svc1"
                               "maxctrl destroy server server5;"
                               "maxctrl destroy listener svc1 listener1;"
                               "maxctrl destroy monitor mon1;"
                               "maxctrl destroy filter qla2;"
                               "maxctrl destroy service svc1;"
                               "maxctrl destroy user maxuser;"
                               "maxctrl stop service RW-Split-Router;"
                               "maxctrl stop monitor MySQL-Monitor;"
                               "maxctrl stop maxscale;"
                               "maxctrl start service RW-Split-Router;"
                               "maxctrl start monitor MySQL-Monitor;"
                               "maxctrl start maxscale;"
                               "maxctrl alter server server1 port 3307;"
                               "maxctrl alter server server1 port 3306;"
                               "maxctrl alter monitor MySQL-Monitor auto_failover true;"
                               "maxctrl alter service RW-Split-Router max_slave_connections=3;"
                               "maxctrl alter logging highprecision true;"
                               "maxctrl alter maxscale passive true;"
                               "maxctrl rotate logs;"
                               "maxctrl call command mariadbmon reset-replication MySQL-Monitor;"
                               "maxctrl api get servers;"
                               "maxctrl classify 'select 1';");

    test.check_maxscale_alive();
    return test.global_result;
}
