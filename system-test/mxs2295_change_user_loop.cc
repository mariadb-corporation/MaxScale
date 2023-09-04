/**
 * MXS-2295: COM_CHANGE_USER does not clear out session command history
 * https://jira.mariadb.org/browse/MXS-2295
 */

#include <maxtest/testconnections.hh>

int main(int argc, char *argv[])
{
    TestConnections test(argc, argv);


    Connection conn = test.maxscale->rwsplit();
    test.expect(conn.connect(), "Connection failed: %s", conn.error());

    for (int i = 0; i <= 300 && test.global_result == 0; i++)
    {
        if (i % 50 == 0)
        {
            test.tprintf("Iteration: %d", i);
        }

        test.reset_timeout();

        // Interleaved session commands, reads and "writes" (`SELECT @@last_insert_id` is treated as a master-only read)
        test.expect(conn.query("SET @a = (SELECT SLEEP(case @@server_id when 1 then 0 else 0.01 end))"), "Query failed: %s", conn.error());
        test.expect(conn.query("USE test"), "Query failed: %s", conn.error());
        test.expect(conn.query("SET SQL_MODE=''"), "Query failed: %s", conn.error());
        test.expect(conn.query("USE test"), "Query failed: %s", conn.error());
        test.expect(conn.query("SELECT @@last_insert_id"), "Query failed: %s", conn.error());
        test.expect(conn.query("SELECT 1"), "Query failed: %s", conn.error());
        test.expect(conn.query("USE test"), "Query failed: %s", conn.error());
        test.expect(conn.query("SELECT 1"), "Query failed: %s", conn.error());

        // User variable inside transaction
        test.expect(conn.query("SET @a = 123"), "Query failed: %s", conn.error());
        test.expect(conn.query("BEGIN"), "Query failed: %s", conn.error());
        Row row = conn.row("SELECT @a");
        test.expect(!row.empty() && row[0] == "123", "Invalid contents in user variable inside RW trx");
        test.expect(conn.query("COMMIT"), "Query failed: %s", conn.error());

        // User variable outside transaction
        test.expect(conn.query("SET @a = 321"), "Query failed: %s", conn.error());
        row = conn.row("SELECT @a");
        test.expect(!row.empty() && row[0] == "321", "Invalid contents in user variable outside trx");

        // User variable inside read-only transaction
        test.expect(conn.query("SET @a = 456"), "Query failed: %s", conn.error());
        test.expect(conn.query("START TRANSACTION READ ONLY"), "Query failed: %s", conn.error());
        row = conn.row("SELECT @a");
        test.expect(!row.empty() && row[0] == "456", "Invalid contents in user variable inside RO trx");
        test.expect(conn.query("COMMIT"), "Query failed: %s", conn.error());

        test.expect(conn.query("PREPARE ps FROM 'SELECT 1'"), "PREPARE failed: %s", conn.error());
        row = conn.row("EXECUTE ps");
        test.expect(!row.empty() && row[0] == "1", "Invalid contents in PS result");
        test.expect(conn.query("DEALLOCATE PREPARE ps"), "DEALLOCATE failed: %s", conn.error());

        test.expect(conn.reset_connection(), "Connection reset failed: %s", conn.error());
    }

    // Wait for the slaves to complete the session commands
    test.tprintf("Waiting for slaves to complete session commands");
    sleep(5);

    auto slave_response = conn.field("SELECT @a", 0);
    auto master_response = conn.field("SELECT @a, @@last_insert_id", 0);

    test.expect(slave_response == master_response, "Slave value '%s' is different from master value '%s'",
                slave_response.c_str(), master_response.c_str());

    test.log_excludes("Router session exceeded session command history limit");
    test.log_includes("Resetting session command history");

    return test.global_result;
}
