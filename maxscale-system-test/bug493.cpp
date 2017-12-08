/**
 * @file bug493.cpp regression case for bug 493 ( Can have same section name multiple times without warning)
 *
 * - Maxscale.cnf in which 'server2' is defined twice and tests checks error log for proper error message.
 * - check if Maxscale is alive
 */

/*
Hartmut Holzgraefe 2014-08-31 21:01:06 UTC
Due to a copy/paste error I ended up with two [server2] sections instead of having [server2] and [server3].

There were no error or warning messages about this in skygw_err1.log or skygw_msg1.log but only the second [server2] was actually used.


Configuration file:

---8<------------------
[maxscale]
threads=1

[CLI]
type=service
router=cli

[CLI Listener]
type=listener
service=CLI
protocol=maxscaled
address=localhost
port=6603

[server1]
type=server
address=127.0.0.1
port=3000
protocol=MySQLBackend

[server2]
type=server
address=127.0.0.1
port=3001
protocol=MySQLBackend

[server2]
type=server
address=127.0.0.1
port=3002
protocol=MySQLBackend
-------->8---

maxadmin results:


---8<--------------------
MaxScale> list servers
Servers.
-------------------+-----------------+-------+----------------------+------------
Server             | Address         | Port  | Status               | Connections
-------------------+-----------------+-------+----------------------+------------
server1            | 127.0.0.1       |  3000 | Running              |    0
server2            | 127.0.0.1       |  3002 | Running              |    0
-------------------+-----------------+-------+----------------------+------------
------------->8---

So no entry for the first (actually correct) [server2] on port 3001,
but only for the duplicate 2nd [server2] with port set to 3002 ...
Comment 1 Mark Riddoch 2014-09-01 16:17:51 UTC
The ini file parser we use allows multiple sections with the same name and will combine the section contains. Within this restriction we now have added code that will detect the same parameter being set twice and will warn the user.

*/


#include <iostream>
#include <unistd.h>
#include "testconnections.h"

using namespace std;

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);
    Test->set_timeout(10);
    Test->check_log_err(0, (char *) "Duplicate section found: server2", true);
    Test->check_log_err(0, (char *)
                        "Failed to open, read or process the MaxScale configuration file /etc/maxscale.cnf. Exiting", true);
    //Test->check_maxscale_alive(0);
    int rval = Test->global_result;
    delete Test;
    return rval;
}
