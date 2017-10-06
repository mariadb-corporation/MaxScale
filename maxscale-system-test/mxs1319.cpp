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
    test.maxscales->restart_maxscale(0);

    test.tprintf("Connecting to MaxScale and executing a query");
    test.maxscales->connect_maxscale(0);
    test.try_query(test.maxscales->conn_rwsplit[0], "SELECT 1");
    test.maxscales->close_maxscale_connections(0);

    test.repl->execute_query_all_nodes("SET GLOBAL SQL_MODE=DEFAULT");
    return test.global_result;
}
