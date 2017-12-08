/**
 * @file bug626.cpp  regression case for bug 626 ("Crash when user define with old password style (before 4.1 protocol)"), also checks error message in the log for bug428 ("Pre MySQL 4.1 encrypted passwords cause authorization failure")
 *
 * - CREATE USER 'old'@'%' IDENTIFIED BY 'old';
 * - SET PASSWORD FOR 'old'@'%' = OLD_PASSWORD('old');
 * - try to connect using user 'old'
 * - check log for "MaxScale does not support these old passwords" warning
 * - DROP USER 'old'@'%'
 * - check MaxScale is alive
 */

/*
Stephane VAROQUI 2014-11-25 17:37:58 UTC
2014-11-21 16:24:03   Error : Invalid authentication message from backend. Error code: 1129, Msg : Host '192.168.42.172' is blocked because of many connection errors; unblock with 'mysqladmin flush-hosts'
2014-11-21 16:24:03   Error : access for secrets file [/usr/local/maxscale/maxscale-1.0.1-beta/etc/.secrets] failed. Error 2, No such file or directory.
2014-11-21 16:24:03   Error : Unable to get user data from backend database for service RW Split Router. Missing server information.
2014-11-21 16:24:03   Error : Unable to write to backend due to authentication failure.
2014-11-21 16:24:03   Error : Invalid authentication message from backend. Error code: 1129, Msg : Host '192.168.42.172' is blocked because of many connection errors; unblock with 'mysqladmin flush-hosts'
2014-11-21 16:24:03   Error : access for secrets file [/usr/local/maxscale/maxscale-1.0.1-beta/etc/.secrets] failed. Error 2, No such file or directory.
2014-11-21 16:24:03   Error : Unable to get user data from backend database for service RW Split Router. Missing server information.
2014-11-21 16:24:03   Error : Unable to write to backend due to authentication failure.
2014-11-21 16:24:03   Fatal: MaxScale received fatal signal 11. Attempting backtrace.
2014-11-21 16:24:03     ./maxscale() [0x53ad1c]

2014-11-21 16:24:03     /usr/lib64/libpthread.so.0(+0xf6d0) [0x7fd8039756d0]

2014-11-21 16:24:03     /usr/local/maxscale/maxscale-1.0.1-beta/modules/libreadwritesplit.so(is_read_tmp_table+0x64) [0x7fd7ec0f9d25]

2014-11-21 16:24:03     /usr/local/maxscale/maxscale-1.0.1-beta/modules/libreadwritesplit.so(+0x5577) [0x7fd7ec0fa577]

2014-11-21 16:24:03     /usr/local/maxscale/maxscale-1.0.1-beta/modules/libMySQLClient.so(+0x5821) [0x7fd7ea1a1821]

2014-11-21 16:24:03     /usr/local/maxscale/maxscale-1.0.1-beta/modules/libMySQLClient.so(+0x49df) [0x7fd7ea1a09df]

2014-11-21 16:24:03     ./maxscale() [0x547093]

2014-11-21 16:24:03     ./maxscale(poll_waitevents+0xbd) [0x546858]

2014-11-21 16:24:03     ./maxscale(main+0x12b2) [0x53cfd2]

2014-11-21 16:24:03     /usr/lib64/libc.so.6(__libc_start_main+0xf5) [0x7fd8035c9d65]

2014-11-21 16:24:03     ./maxscale() [0x539fdd]
Comment 1 Mark Riddoch 2014-11-25 17:57:20 UTC
Vilho,

looks like multiple issues here, the missing authentication data is one problem, but the SEGFAULT appears to occur in the Read/Write Splitter

2014-11-21 16:24:03   Fatal: MaxScale received fatal signal 11. Attempting backtrace.
2014-11-21 16:24:03     ./maxscale() [0x53ad1c]

2014-11-21 16:24:03     /usr/lib64/libpthread.so.0(+0xf6d0) [0x7fd8039756d0]

2014-11-21 16:24:03     /usr/local/maxscale/maxscale-1.0.1-beta/modules/libreadwritesplit.so(is_read_tmp_table+0x64) [0x7fd7ec0f9d25]

2014-11-21 16:24:03     /usr/local/maxscale/maxscale-1.0.1-beta/modules/libreadwritesplit.so(+0x5577) [0x7fd7ec0fa577]

2014-11-21 16:24:03     /usr/local/maxscale/maxscale-1.0.1-beta/modules/libMySQLClient.so(+0x5821) [0x7fd7ea1a1821]

Massimiliano 2014-12-01 17:29:26 UTC
I have found an easy way to produce the "Host xxxx is blocked because of many connection errors; unblock with 'mysqladmin flush-hosts'" error


Connect directly to mysql backend(s):

# mysql -h 127.0.0.1 -P 3310
MariaDB [(none)]> set global max_connect_errors=1;
Query OK, 0 rows affected (0.00 sec)

...

# nc 127.0.0.1 3310
]
5.5.5-10.0.11-MariaDB-log??A[(SHQ>$???6$PEI"ilc+L{mysql_native_password

Ctrl-C

Next attempt results in:

 # nc 127.0.0.1 3310
j?iHost '151.20.6.153' is blocked because of many connection errors; unblock with 'mysqladmin flush-hosts'[root@mcentos62 ~]


Then I restored error count:

# mysqladmin -h  127.0.0.1 -P 3310 flush-hosts



I launched a mysqlslap test against MaxScale, and after a few seconds I caused the error as described above and ...

[root@mcentos62 ~]# mysqlslap -h 127.0.0.1 -P 4008 -umassi -pmassi --query="select 1" --concurrency=16 --iterations=200
mysqlslap: Cannot run query select 1 ERROR : Authentication with backend failed. Session will be closed.

But no crash at all, this with GA branch and maxscale-1.0.1-beta RPMs.


Some details about the error itself.

http://dev.mysql.com/doc/refman/5.0/en/blocked-host.html

B.5.2.6 Host 'host_name' is blocked
If the following error occurs, it means that mysqld has received many connection requests from the given host that were interrupted in the middle:

...

By default, mysqld blocks a host after 10 connection errors.


------------------------------------

I also tried with with gdb and MaxScale binary, with a breakpoint set to the gw_create_backend_connection routine.

Once the breakpoint is reached I did Ctrl-C  int the mysql client (connected to MaxScale) and this caused the error too in the next connection.

As the client stopped running, MaxScale cannot continue with async backend connection and this may increase the error counter: this may be a good example looking for any possible incomplete backend authentication due to a potential bug.


Note, using a value as high as 10000 for max_connect_errors doesn't result in any issue of course.


BTW, in my today setup even having backends with max_connect_errors=1 doesn't result in any issue at all.

I run a test with MaxScale on a Virtual CentOS 6.2 on my laptop and backends in a Digital Ocean server, so with the Internet in the middle.
Comment 9 Massimiliano 2014-12-03 10:04:49 UTC
Reported segfault is related to is_read_tmp_table() routine.


"many connection errors" not spotted yet during MaxScale tests


A new setup is highly desiderable, it should happen in a few days.
Comment 10 Massimiliano 2014-12-03 15:48:55 UTC
No issues/crea found with user and old_password style.

Message is logged into the error log where there is such case.
*/


#include <iostream>
#include "testconnections.h"

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);
    Test->set_timeout(20);

    printf("Creating user with old style password\n");
    Test->repl->connect();
    execute_query(Test->repl->nodes[0], "CREATE USER 'old'@'%%' IDENTIFIED BY 'old';");
    execute_query(Test->repl->nodes[0], "SET PASSWORD FOR 'old'@'%%' = OLD_PASSWORD('old');");
    Test->stop_timeout();
    Test->repl->sync_slaves();

    Test->set_timeout(20);
    printf("Trying to connect using user with old style password\n");
    MYSQL * conn = open_conn(Test->maxscales->rwsplit_port[0], Test->maxscales->IP[0], (char *) "old", (char *)  "old", Test->ssl);

    if ( mysql_errno(conn) != 0)
    {
        Test->tprintf("Connections is not open as expected\n");
    }
    else
    {
        Test->add_result(1, "Connections is open for the user with old style password.\n");
    }
    if (conn != NULL)
    {
        mysql_close(conn);
    }

    execute_query(Test->repl->nodes[0], "DROP USER 'old'@'%%'");

    Test->check_log_err(0, (char *) "MaxScale does not support these old passwords", true);
    Test->check_maxscale_alive(0);

    int rval = Test->global_result;
    delete Test;
    return rval;
}

