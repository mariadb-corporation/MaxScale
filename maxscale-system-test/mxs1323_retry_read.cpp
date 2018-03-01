/**
 * Test for MXS-1323.
 * - Check that retried reads work with persistent connections
 */

#include "testconnections.h"

void* async_block(void* data)
{
    TestConnections *test = (TestConnections*)data;
    sleep(5);
    test->tprintf("Blocking slave");
    test->repl->block_node(1);
    return NULL;
}

std::string do_query(TestConnections& test)
{
    MYSQL* conn = test.open_rwsplit_connection();

    const char* query = "SELECT SLEEP(15), @@server_id";
    char output[512] = "";

    find_field(conn, query, "@@server_id", output);
    mysql_close(conn);

    return std::string(output);
}

int main(int argc, char *argv[])
{
    TestConnections test(argc, argv);

    char server_id[2][1024];
    test.repl->connect();
    std::string master = test.repl->get_server_id_str(0);
    std::string slave = test.repl->get_server_id_str(1);
    test.repl->close_connections();

    test.set_timeout(60);
    std::string res = do_query(test);
    test.add_result(res != slave, "The slave should respond to the first query: %s", res.c_str());

    pthread_t thr;
    pthread_create(&thr, NULL, async_block, &test);
    res = do_query(test);
    test.add_result(res != master, "The master should respond to the second query: %s", res.c_str());
    pthread_join(thr, NULL);
    test.repl->unblock_node(1);

    return test.global_result;
}
