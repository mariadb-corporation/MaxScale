/**
 * @file bug620.cpp bug620 regression case ("enable_root_user=true generates errors to error log")
 *
 * - Maxscale.cnf contains RWSplit router definition with enable_root_user=true
 * - GRANT ALL PRIVILEGES ON *.* TO 'root'@'%' IDENTIFIED BY 'skysqlroot';
 * - try to connect using 'root' user and execute some query
 * - errors are not expected in the log. All Maxscale services should be alive.
 */

/*
 *  Vilho Raatikka 2014-11-14 09:03:59 UTC
 *  Enabling use of root user in MaxScale causes the following being printed to error log. Disabling the
 * setting enable_root_user prevents these errors.
 *
 *  2014-11-14 11:02:47   Error : getaddrinfo failed for [linux-yxkl.site] due [Name or service not known]
 *  2014-11-14 11:02:47   140635119954176 [mysql_users_add()] Failed adding user root@linux-yxkl.site for
 * service [RW Split Router]
 *  2014-11-14 11:02:47   Error : getaddrinfo failed for [::1] due [Address family for hostname not supported]
 *  2014-11-14 11:02:47   140635119954176 [mysql_users_add()] Failed adding user root@::1 for service [RW
 * Split Router]
 *  2014-11-14 11:02:47   140635119954176 [mysql_users_add()] Failed adding user root@127.0.0.1 for service
 * [RW Split Router]
 *  Comment 1 Vilho Raatikka 2014-11-14 09:04:40 UTC
 *  This appears with binary built from develop branch 14.11.14
 *  Comment 2 Massimiliano 2014-11-14 09:15:27 UTC
 *  The messages appear in the log because root user has an invalid hostname: linux-yxkl.site
 *
 *
 *  The second message root@127.0.0.1 may be related to a previous root@localhost entry.
 *
 *
 *  Names are resolved to IPs and added into maxscale hashtable: localhost and 127.0.0.1 result in a
 * duplicated entry
 *
 *
 *
 *  A standard root@localhost only entry doesn't cause any logged message
 *  Comment 3 Vilho Raatikka 2014-11-14 09:24:56 UTC
 *  Problem is that they seem critical errors but MaxScale still works like nothing had happened. If the
 * default hostname of the server host is not good, what does it mean for MaxScale? Doest it still accept root
 * user or not? Why it only causes trouble for root user but not for others?
 *
 *  If the error has no effect in practice, then log entries could be better in debug log.
 *
 *  Thread ids are suitable in debug log but not anywhere else.
 *  Comment 4 Massimiliano 2014-11-14 09:32:27 UTC
 *  The 'enable_root_user' option only allows selecting 'root' user from backend databases.
 *
 *
 *  The error messages are printed for all users and report that
 *
 *  specific_user@specific_host is not loaded but
 *
 *
 *  Example:
 *
 *  2014-11-14 11:02:47   Error : getaddrinfo failed for [linux-yxkl.site] due [Name or service not known]
 *  2014-11-14 11:02:47   140635119954176 [mysql_users_add()] Failed adding user root@linux-yxkl.site for
 * service [RW Split Router]
 *
 *  2014-11-14 04:19:23   Error : getaddrinfo failed for [ftp.*.fi] due [Name or service not known]
 *  2014-11-14 04:19:23   67322400 [mysql_users_add()] Failed adding user foo@ftp.*.fi for service [Master
 * Service]
 *
 *
 *
 *  In the examples foo@%.funet.fi and root@linux-yxkl.site are not loaded.
 *
 *
 *  foo@localhost and root@localhost are loaded
 *  Comment 5 Vilho Raatikka 2014-11-14 10:55:35 UTC
 *  (In reply to comment #4)
 *  > The 'enable_root_user' option only allows selecting 'root' user from backend
 *  > databases.
 *
 *  I think that enable_root_user means : MaxScale user can use her 'root' account also with MaxScale.
 *
 *  Technically your explanation may be correct and I'm not against that. What I mean is that the user may not
 * want to worry about what is 'loaded' or 'selected' under the cover.
 *  She simply wants to use root user. If it is not possible then that should be written to error log clearly.
 * For example, "Use of 'root' disabled due to unreachable hostname" or something equally clear.
 *
 *  Reporting several lines of errors may confuse the user especially if the root account still works
 * perfectly.
 *
 *  >
 *  >
 *  > The error messages are printed for all users and report that
 *  >
 *  >  specific_user@specific_host is not loaded but
 *  >
 *  >
 *  > Example:
 *  >
 *  > 2014-11-14 11:02:47   Error : getaddrinfo failed for [linux-yxkl.site] due
 *  > [Name or service not known]
 *  > 2014-11-14 11:02:47   140635119954176 [mysql_users_add()] Failed adding user
 *  > root@linux-yxkl.site for service [RW Split Router]
 *  >
 *  > 2014-11-14 04:19:23   Error : getaddrinfo failed for [ftp.*.fi] due [Name or
 *  > service not known]
 *  > 2014-11-14 04:19:23   67322400 [mysql_users_add()] Failed adding user
 *  > foo@ftp.*.fi for service [Master Service]
 *  >
 *  >
 *  >
 *  > In the examples foo@%.funet.fi and root@linux-yxkl.site are not loaded.
 *  >
 *  >
 *  > foo@localhost and root@localhost are loaded
 *  Comment 6 Massimiliano 2014-11-14 11:00:04 UTC
 *  MaxScale MySQL authentication is based on user@host
 *
 *
 *  You may have such situation:
 *
 *  foo@localhost [OK]
 *  foo@x-y-z.notexists [KO]
 *
 *  user 'foo@localhost' is loaded the latter isn't
 *
 *
 *  For root user is the same.
 *
 *  Allowing selection of root user means selecting all the rows from mysql.user table where user='root'
 *
 *
 *  if there is any row (root@xxxx) that cannot be loaded the message appears.
 *
 *  In a standard setup we don't expect any log messages
 *  Comment 7 Timofey Turenko 2014-12-10 16:02:36 UTC
 *  I tried following:
 *
 *  via RWSplit:
 *
 *  GRANT ALL PRIVILEGES ON *.* TO 'root'@'%' IDENTIFIED BY 'skysqlroot';
 *
 *  and try to connect to RWSplit using 'root' and 'skysqlroot' and try some simple query:
 *
 *  2014-12-10 17:35:43   Error : getaddrinfo failed for [::1] due [Address family for hostname not supported]
 *  2014-12-10 17:35:43   Warning: Failed to add user root@::1 for service [RW Split Router]. This user will
 * be unavailable via MaxScale.
 *  2014-12-10 17:35:43   Warning: Failed to add user root@127.0.0.1 for service [RW Split Router]. This user
 * will be unavailable via MaxScale.
 *  2014-12-10 17:35:43   Error : Failed to start router for service 'HTTPD Router'.
 *  2014-12-10 17:35:43   Error : Failed to start service 'HTTPD Router'.
 *  2014-12-10 17:36:08   Error : getaddrinfo failed for [::1] due [Address family for hostname not supported]
 *  2014-12-10 17:36:08   Warning: Failed to add user root@::1 for service [RW Split Router]. This user will
 * be unavailable via MaxScale.
 *  2014-12-10 17:36:08   Warning: Failed to add user root@127.0.0.1 for service [RW Split Router]. This user
 * will be unavailable via MaxScale.
 *
 *
 *  Is it expected?
 *  Comment 8 Massimiliano 2014-12-10 16:09:23 UTC
 *  root@::1 could not be loaded because it's for IPv6
 *
 *  root@127.0.0.1 may be not loaded if root@localhost was found before
 *
 *  As names are translated to IPv4 addresses localhost->127.0.0.1 and that'a duplicate
 *
 *
 *  2014-12-10 17:35:43   Error : Failed to start router for service 'HTTPD Router'.
 *  2014-12-10 17:35:43   Error : Failed to start service 'HTTPD Router'.
 *
 *  Those messages are not part of mysql users load phase.
 *
 *  when you have auth errors users are reload (in the allowed time window) and you see the messages again
 *
 *
 *  With admin interface you can check:
 *
 *
 *  show dbusers RW Split Router
 *
 *  and you should see root@% you added with the grant
 *  Comment 9 Timofey Turenko 2014-12-12 21:59:30 UTC
 *  Following is present in the error log just after MaxScale start:
 *
 *
 *  2014-12-12 23:49:07   Error : getaddrinfo failed for [::1] due [Address family for hostname not supported]
 *  2014-12-12 23:49:07   Warning: Failed to add user root@::1 for service [RW Split Router]. This user will
 * be unavailable via MaxScale.
 *  2014-12-12 23:49:07   Warning: Failed to add user root@127.0.0.1 for service [RW Split Router]. This user
 * will be unavailable via MaxScale.
 *
 *
 *  first two line are clear: no support for IPv6, but would it be better to print 'warning' instead of
 * 'error'?
 *
 *  "Failed to add user root@127.0.0.1" - is it correct?
 *
 *  direct connection to backend gives:
 *  MariaDB [(none)]> select User, host from mysql.user;
 +---------+-----------+
 | User    | host      |
 +---------+-----------+
 | maxuser | %         |
 | repl    | %         |
 | skysql  | %         |
 | root    | 127.0.0.1 |
 | root    | ::1       |
 |         | localhost |
 | maxuser | localhost |
 | root    | localhost |
 | skysql  | localhost |
 |         | node1     |
 | root    | node1     |
 +---------+-----------+
 |
 |  admin interface gives:
 |
 |  MaxScale> show dbusers "RW Split Router"
 |  Users table data
 |  Hashtable: 0x7f6b64000c30, size 52
 |   No. of entries:         7
 |   Average chain length:   0.1
 |   Longest chain length:   1
 |  User names: root@192.168.122.106, repl@%, skysql@%, maxuser@127.0.0.1, skysql@127.0.0.1, root@127.0.0.1,
 |maxuser@%
 |
 |
 |  So, root@127.0.0.1 is present in the list.
 |  Comment 10 Mark Riddoch 2015-01-05 13:03:34 UTC
 |  The message "Failed to add user root@127.0.0.1" is because the two entries root@localhsot and
 |root@127.0.0.1 are seen as duplicates in MaxScale. This is a result of MaxScale resolving hostnames at the
 |time it reads the database rather than at connect time. So a duplicate is detected and the second one causes
 |the error to be displayed.
 |  Comment 11 Timofey Turenko 2015-01-09 19:26:35 UTC
 |  works as expected, closing.
 |  Check for lack of "Error : getaddrinfo failed" added (just in case) and for warning about 'skysql'
 */



#include <iostream>
#include <unistd.h>
#include "testconnections.h"

using namespace std;

int main(int argc, char* argv[])
{
    TestConnections* Test = new TestConnections(argc, argv);
    Test->set_timeout(30);

    Test->maxscales->connect_maxscale(0);

    Test->tprintf("Creating 'root'@'%%'\n");
    // global_result += execute_query(Test->maxscales->conn_rwsplit[0], (char *) "CREATE USER 'root'@'%'; SET
    // PASSWORD FOR 'root'@'%' = PASSWORD('skysqlroot');");

    Test->try_query(Test->maxscales->conn_rwsplit[0],
                    (char*) "GRANT ALL PRIVILEGES ON *.* TO 'root'@'%%' IDENTIFIED BY 'skysqlroot';");
    Test->try_query(Test->maxscales->conn_rwsplit[0],
                    (char*) "GRANT ALL PRIVILEGES ON *.* TO 'root'@'localhost' IDENTIFIED BY 'skysqlroot';");
    sleep(10);

    MYSQL* conn;

    Test->tprintf("Connecting using 'root'@'%%'\n");
    conn = open_conn(Test->maxscales->rwsplit_port[0],
                     Test->maxscales->IP[0],
                     (char*) "root",
                     (char*)  "skysqlroot",
                     Test->ssl);
    if (mysql_errno(conn) != 0)
    {
        Test->add_result(1, "Connection using 'root' user failed, error: %s\n", mysql_error(conn));
    }
    else
    {
        Test->tprintf("Simple query...\n");
        Test->try_query(conn, (char*) "SELECT * from mysql.user");
        Test->try_query(conn,
                        (char*) "set password for 'root'@'localhost' = PASSWORD('');");
    }
    if (conn != NULL)
    {
        mysql_close(conn);
    }

    Test->tprintf("Dropping 'root'@'%%'\n");
    Test->try_query(Test->maxscales->conn_rwsplit[0], (char*) "DROP USER 'root'@'%%';");

    Test->maxscales->close_maxscale_connections(0);

    Test->check_log_err(0, (char*) "Failed to add user skysql", false);
    Test->check_log_err(0, (char*) "getaddrinfo failed", false);
    Test->check_log_err(0, (char*) "Couldn't find suitable Master", false);

    Test->check_maxscale_alive(0);
    int rval = Test->global_result;
    delete Test;
    return rval;
}
