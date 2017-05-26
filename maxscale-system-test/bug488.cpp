/**
 * @file bug488.cpp regression case for bug 488 ("SHOW VARIABLES randomly failing with "Lost connection to MySQL server")
 *
 * - try "SHOW VARIABLES;" 100 times against all Maxscale services
 * First round: 100 iterations for RWSplit, then ReadConn Master, then ReadConn Slave
 * Second round: 100 iteration and in every iterations all three Maxscale services are in use.
 * - check if Maxscale is alive.
 */

/*
Kolbe Kegel 2014-08-27 18:37:14 UTC
Created attachment 138 [details]
good.txt and bad.txt

Sending "SHOW VARIABLES" to MaxScale seems to sometimes result in "ERROR 2013 (HY000) at line 1: Lost connection to MySQL server during query". It appears to be random. It seems to be sending the query to the same backend server, so I'm not sure what is happening. I'm including the debug log for both the "good" and "bad" queries.
Comment 1 Vilho Raatikka 2014-08-27 18:41:25 UTC
Seems to happen exactly every second time with rwsplit router. Didn't experience it with read connection router.
Comment 2 Kolbe Kegel 2014-08-27 18:47:13 UTC
Not exactly every 2nd time.

$ mysql  -h max1 -P 4006 -u maxuser -pmaxpwd -e 'show variables' >/dev/null
$ mysql  -h max1 -P 4006 -u maxuser -pmaxpwd -e 'show variables' >/dev/null
$ mysql  -h max1 -P 4006 -u maxuser -pmaxpwd -e 'show variables' >/dev/null
ERROR 2013 (HY000) at line 1: Lost connection to MySQL server during query
$ mysql  -h max1 -P 4006 -u maxuser -pmaxpwd -e 'show variables' >/dev/null
ERROR 2013 (HY000) at line 1: Lost connection to MySQL server during query
$ mysql  -h max1 -P 4006 -u maxuser -pmaxpwd -e 'show variables' >/dev/null
$ mysql  -h max1 -P 4006 -u maxuser -pmaxpwd -e 'show variables' >/dev/null
ERROR 2013 (HY000) at line 1: Lost connection to MySQL server during query
$ mysql  -h max1 -P 4006 -u maxuser -pmaxpwd -e 'show variables' >/dev/null
ERROR 2013 (HY000) at line 1: Lost connection to MySQL server during query
$ mysql  -h max1 -P 4006 -u maxuser -pmaxpwd -e 'show variables' >/dev/null
ERROR 2013 (HY000) at line 1: Lost connection to MySQL server during query
Comment 3 Vilho Raatikka 2014-08-28 10:48:39 UTC
SHOW VARIABLES is session write command - which is unnecessary because those could be read from any server - and what causes client to return 'lost connection' message to the user is duplicated response packet from MaxScale to the client.

SHOW VARIABLES response should start like this:
T 127.0.0.1:59776 -> 127.0.0.1:4006 [AP]
  0f 00 00 00 03 73 68 6f    77 20 76 61 72 69 61 62    .....show variab
  6c 65 73                                              les

T 127.0.0.1:4006 -> 127.0.0.1:59776 [AP]
  01 00 00 01 02                                        .....

T 127.0.0.1:4006 -> 127.0.0.1:59776 [AP]
  54 00 00 02 03 64 65 66    12 69 6e 66 6f 72 6d 61    T....def.informa
  74 69 6f 6e 5f 73 63 68    65 6d 61 09 56 41 52 49    tion_schema.VARI
  41 42 4c 45 53 09 56 41    52 49 41 42 4c 45 53 0d    ABLES.VARIABLES.
  56 61 72 69 61 62 6c 65    5f 6e 61 6d 65 0d 56 41    Variable_name.VA
  52 49 41 42 4c 45 5f 4e    41 4d 45 0c 08 00 40 00    RIABLE_NAME...@.
  00 00 fd 01 00 00 00 00                               ........

While in the failing case the initial packet is followed something like this:

T 127.0.0.1:59776 -> 127.0.0.1:4006 [AP]
  0f 00 00 00 03 73 68 6f    77 20 76 61 72 69 61 62    .....show variab
  6c 65 73                                              les

T 127.0.0.1:4006 -> 127.0.0.1:59776 [AP]
  1d 00 00 d5 18 69 6e 6e    6f 64 62 5f 75 73 65 5f    .....innodb_use_
  61 74 6f 6d 69 63 5f 77    72 69 74 65 73 03 4f 46    atomic_writes.OF
  46                                                    F

T 127.0.0.1:4006 -> 127.0.0.1:59776 [AP]
  19 00 00 d6 14 69 6e 6e    6f 64 62 5f 75 73 65 5f    .....innodb_use_
  66 61 6c 6c 6f 63 61 74    65 03 4f 46 46             fallocate.OFF

- where those innodb related packets are duplicates from the previous response.

*/



#include <iostream>
#include "testconnections.h"

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);
    Test->set_timeout(10);
    int i;

    Test->repl->connect();
    Test->connect_maxscale();

    Test->tprintf("Trying SHOW VARIABLES to different Maxscale services\n");
    fflush(stdout);
    Test->tprintf("RWSplit\n");
    for (i = 0; i < 100; i++)
    {
        Test->set_timeout(5);
        Test->try_query(Test->conn_rwsplit, (char *) "SHOW VARIABLES;");
    }
    Test->tprintf("ReadConn master\n");
    for (i = 0; i < 100; i++)
    {
        Test->set_timeout(5);
        Test->try_query(Test->conn_master, (char *) "SHOW VARIABLES;");
    }
    Test->tprintf("ReadConn slave\n");
    for (i = 0; i < 100; i++)
    {
        Test->set_timeout(5);
        Test->try_query(Test->conn_slave, (char *) "SHOW VARIABLES;");
    }

    Test->tprintf("All in one loop\n");
    for (i = 0; i < 100; i++)
    {
        Test->set_timeout(5);
        Test->try_query(Test->conn_rwsplit, (char *) "SHOW VARIABLES;");
        Test->try_query(Test->conn_master, (char *) "SHOW VARIABLES;");
        Test->try_query(Test->conn_slave, (char *) "SHOW VARIABLES;");
    }

    Test->set_timeout(10);
    Test->close_maxscale_connections();
    Test->repl->close_connections();

    Test->check_maxscale_alive();

    int rval = Test->global_result;
    delete Test;
    return rval;
}
