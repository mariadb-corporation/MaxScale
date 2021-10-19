#include <maxtest/testconnections.hh>

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);
    test.repl->execute_query_all_nodes("SET GLOBAL session_track_transaction_info=CHARACTERISTICS");


    auto c = test.maxscale->rwsplit();
    c.connect();
    test.tprintf("Disable autocommit and sleep for a while to make sure all servers have executed it");
    c.query("SET autocommit=0");
    sleep(2);

    test.repl->connect();
    auto expected = test.repl->get_server_id_str(0);
    auto id = c.field("SELECT @@server_id");
    test.expect(id == expected, "Expected @@server_id from %s, not from %s", expected.c_str(), id.c_str());

    test.repl->execute_query_all_nodes("SET GLOBAL session_track_transaction_info=OFF");
    return test.global_result;
}
