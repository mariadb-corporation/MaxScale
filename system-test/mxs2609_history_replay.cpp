/**
 * MXS-2609: Maxscale crash in RWSplitSession::retry_master_query()
 *
 * https://jira.mariadb.org/browse/MXS-2609
 *
 * This test attempts to reproduce the crash described in MXS-2609 which
 * occurred during a retrying attempt of a session command that failed on
 * the master.
 */


#include <maxtest/testconnections.hh>

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);

    auto block = [&test](int n) {
            test.repl->block_node(n);
            test.maxscale->wait_for_monitor();
            test.repl->unblock_node(n);
            test.maxscale->wait_for_monitor();
        };
    auto conn = test.maxscale->rwsplit();

    test.log_printf("Test 1: Master failure mid-reconnect should trigger query replay");

    test.expect(conn.connect(), "First connect should work: %s", conn.error());

    test.log_printf("Queue up session commands so that the history replay takes some time");

    for (int i = 0; i < 10; i++)
    {
        conn.query("SET @a = (SELECT SLEEP(1))");
    }

    test.log_printf("Block the master, wait for 5 second and then block it again");
    block(0);

    test.reset_timeout();

    std::thread([&]() {
                    sleep(5);
                    block(0);
                }).detach();

    test.expect(conn.query("SELECT @@last_insert_id"), "Query should work: %s", conn.error());

    conn.disconnect();

    test.log_printf("Test 2: Exceed history limit and trigger a master reconnection");

    test.maxctrl("alter service RW-Split-Router max_sescmd_history 2 prune_sescmd_history false");
    test.expect(conn.connect(), "Second should work: %s", conn.error());

    for (int i = 0; i < 5; i++)
    {
        conn.query("SET @a = (SELECT SLEEP(1))");
    }

    test.log_printf("Block the master, the next query should fail");
    block(0);

    test.expect(!conn.query("SELECT @@last_insert_id"), "Query should fail");

    return test.global_result;
}
