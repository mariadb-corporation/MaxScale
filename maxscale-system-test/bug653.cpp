/**
 * @file bug653.cpp  regression case for bug 653 ("Memory corruption when users with long hostnames that can
 * no the resolved are loaded into MaxScale")
 *
 * - CREATE USER
 *'user_with_very_long_hostname'@'very_long_hostname_that_can_not_be_resolved_and_it_probably_caused_crash.com.net.org'
 * IDENTIFIED BY 'old';
 * - try to connect using user 'user_with_very_long_hostname'
 * - DROP USER
 *'user_with_very_long_hostname'@'very_long_hostname_that_can_not_be_resolved_and_it_probably_caused_crash.com.net.org'
 * - check MaxScale is alive
 */

/*
 *  Mark Riddoch 2014-12-16 13:17:25 UTC
 *  Program received signal SIGSEGV, Segmentation fault.
 *  0x00007ffff49385ac in free () from /lib64/libc.so.6
 *  Missing separate debuginfos, use: debuginfo-install glibc-2.12-1.47.el6_2.12.x86_64
 * keyutils-libs-1.4-4.el6.x86_64 krb5-libs-1.10.3-10.el6_4.2.x86_64 libaio-0.3.107-10.el6.x86_64
 * libcom_err-1.41.12-14.el6.x86_64 libgcc-4.4.7-4.el6.x86_64 libselinux-2.0.94-5.3.el6_4.1.x86_64
 * libstdc++-4.4.7-4.el6.x86_64 nss-pam-ldapd-0.7.5-14.el6_2.1.x86_64
 * nss-softokn-freebl-3.14.3-10.el6_5.x86_64 openssl-1.0.1e-16.el6_5.15.x86_64 zlib-1.2.3-29.el6.x86_64
 *  (gdb) where
 #0  0x00007ffff49385ac in free () from /lib64/libc.so.6
 #1  0x000000000041d421 in add_mysql_users_with_host_ipv4 (users=0x72c4c0,
 *   user=0x739030 "u3", host=0x739033 "aver.log.hostname.to.overflow.the.buffer",
 *   passwd=0x73905c "", anydb=0x739089 "Y", db=0x0) at dbusers.c:291
 #2  0x000000000041e302 in getUsers (service=0x728ef0, users=0x72c4c0)
 *   at dbusers.c:742
 #3  0x000000000041cf97 in load_mysql_users (service=0x728ef0) at dbusers.c:99
 #4  0x00000000004128c7 in serviceStartPort (service=0x728ef0, port=0x729b70)
 *   at service.c:227
 #5  0x0000000000412e27 in serviceStart (service=0x728ef0) at service.c:365
 #6  0x0000000000412f00 in serviceStartAll () at service.c:413
 #7  0x000000000040b592 in main (argc=2, argv=0x7fffffffe108) at gateway.c:1750
 *  Comment 1 Mark Riddoch 2014-12-16 13:18:09 UTC
 *  The problem is a buffer overrun in normalise_hostname. Fix underway.
 *  Comment 2 Mark Riddoch 2014-12-16 15:45:59 UTC
 *  Increased buffer size to prevent overrun issue
 *  Comment 3 Timofey Turenko 2014-12-22 15:39:32 UTC
 *  I'm not sure I understand the bug correctly.
 *  But 60-chars long host name does not cause problem (longer is not possible "String
 * 'very_long_hostname_that_can_not_be_resolved_and_it_probably_caused_cra' is too long for host name (should
 * be no longer than 60)"
 */


#include <iostream>
#include "testconnections.h"

int main(int argc, char* argv[])
{
    TestConnections* Test = new TestConnections(argc, argv);
    Test->set_timeout(50);
    Test->maxscales->connect_maxscale(0);

    Test->tprintf("Creating user with old style password\n");
    Test->try_query(Test->maxscales->conn_rwsplit[0],
                    (char*) "CREATE USER 'user_long_host11'@'very_long_hostname_that_probably_caused_crashhh.com.net.org' IDENTIFIED BY 'old'");
    Test->try_query(Test->maxscales->conn_rwsplit[0],
                    (char*) "GRANT ALL PRIVILEGES ON *.* TO 'user_long_host11'@'very_long_hostname_that_probably_caused_crashhh.com.net.org' WITH GRANT OPTION");
    sleep(10);

    Test->tprintf("Trying to connect using user with old style password\n");
    MYSQL* conn = open_conn(Test->maxscales->rwsplit_port[0],
                            Test->maxscales->IP[0],
                            (char*) "user_long_host11",
                            (char*)  "old",
                            Test->ssl);

    if (mysql_errno(conn) != 0)
    {
        Test->tprintf("Connections is not open as expected\n");
    }
    else
    {
        Test->add_result(1, "Connections is open for the user with bad host\n");
    }
    if (conn != NULL)
    {
        mysql_close(conn);
    }

    Test->try_query(Test->maxscales->conn_rwsplit[0],
                    (char*) "DROP USER 'user_long_host11'@'very_long_hostname_that_probably_caused_crashhh.com.net.org'");
    Test->maxscales->close_maxscale_connections(0);

    Test->check_maxscale_alive(0);

    int rval = Test->global_result;
    delete Test;
    return rval;
}
