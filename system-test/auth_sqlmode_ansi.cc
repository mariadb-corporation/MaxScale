/**
 * Regression case for bug 705 ("Authentication fails when the user connects to a database
 * when the SQL mode includes ANSI_QUOTES")
 *
 * - use only one backend
 * - SET GLOBAL sql_mode="ANSI"
 * - restart MaxScale
 * - check log for "Error : Loading database names for service RW_Split encountered error: Unknown column"
 */

#include <maxtest/testconnections.hh>

int main(int argc, char* argv[])
{
    TestConnections test(argc, argv);
    test.repl->connect();
    execute_query(test.repl->nodes[0], "SET GLOBAL sql_mode=\"ANSI\"");

    test.maxscale->restart_maxscale();
    test.log_excludes("Loading database names");
    test.log_excludes("Unknown column");

    execute_query(test.repl->nodes[0], "SET GLOBAL sql_mode=DEFAULT");

    return test.global_result;
}
