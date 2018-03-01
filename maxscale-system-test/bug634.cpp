/**
 * @file bug634.cpp regression case for bug 634 ("SHOW SLAVE STATUS in RW SPLITTER is send to master")
 *
 * - execute SHOW SLAVE STATUS and check resut
 */

/*

Description Stephane VAROQUI 2014-12-03 10:41:30 UTC
SHOW SLAVE STATUS in RW SPLITTER is send to master ?  That could break some monitoring scripts for generic proxy abstraction .
Comment 1 Vilho Raatikka 2014-12-03 11:10:12 UTC
COM_SHOW_SLAVE_STAT was unknown to query classifier. Being fixed.
Comment 2 Vilho Raatikka 2014-12-03 11:26:17 UTC
COM_SHOW_SLAVE_STAT wasn't classified but it was treated as 'unknown' and thus routed to master.
*/


#include <iostream>
#include "testconnections.h"

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);
    Test->set_timeout(5);

    char master_ip[100];

    Test->connect_maxscale();

    for (int i = 0; i < 100; i++)
    {
        Test->set_timeout(5);
        Test->add_result(find_field(Test->conn_rwsplit, (char *) "SHOW SLAVE STATUS", (char *) "Master_Host",
                                    master_ip), "Master_host files is not found in the SHOW SLAVE STATUS reply, probably query went to master\n");
        Test->add_result(strcmp(master_ip, Test->repl->IP_private[0]), "Master IP is wrong\n");
    }

    Test->close_maxscale_connections();
    int rval = Test->global_result;
    delete Test;
    return rval;
}
