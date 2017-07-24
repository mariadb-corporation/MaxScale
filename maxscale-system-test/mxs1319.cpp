/**
 * Check that SQL_MODE='PAD_CHAR_TO_FULL_LENGTH' doesn't break authentication
 */

#include "testconnections.h"

int main(int argc, char *argv[])
{
    TestConnections test(argc, argv);

    test.tprintf("Changing SQL_MODE to PAD_CHAR_TO_FULL_LENGTH and restarting MaxScale");
    test.repl->connect();
    test.repl->execute_query_all_nodes("SET GLOBAL SQL_MODE='PAD_CHAR_TO_FULL_LENGTH'");
    test.restart_maxscale();

    test.tprintf("Connecting to MaxScale and executing a query");
    test.connect_maxscale();
    test.try_query(test.conn_rwsplit, "SELECT 1");
    test.close_maxscale_connections();

    test.repl->execute_query_all_nodes("SET GLOBAL SQL_MODE=DEFAULT");
    return test.global_result;
}
