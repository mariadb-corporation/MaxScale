/**
 * MXS-3769: Support for SET TRANSACTION
 *
 * https://jira.mariadb.org/browse/MXS-3769
 */

#include <maxtest/testconnections.hh>

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);

    test.repl->connect();
    std::string master = test.repl->get_server_id_str(0);

    auto rws = test.maxscale->rwsplit();
    test.expect(rws.connect(), "Failed to connect: %s", rws.error());

    auto do_trx = [&](std::string trx_sql) {
            rws.query(trx_sql);
            std::string id = rws.field("SELECT @@server_id");
            rws.query("COMMIT");
            return id;
        };

    auto master_trx = [&](std::string trx_sql) {
            auto id = do_trx(trx_sql);
            test.expect(id == master,
                        "Response for transaction should come from server with ID %s, not %s",
                        master.c_str(), id.c_str());
        };

    auto slave_trx = [&](std::string trx_sql) {
            test.expect(do_trx(trx_sql) != master,
                        "Response for transaction should not come from server with ID %s",
                        master.c_str());
        };

    // SET TRANSACTION affects only the next transaction. As the default access mode is READ WRITE, the next
    // transaction should be routed to a slave server.
    rws.query("SET TRANSACTION READ ONLY");
    slave_trx("START TRANSACTION");
    master_trx("START TRANSACTION");

    // Changing the default access mode should cause transactions to be routed to slave servers unless an
    // explicit READ WRITE transaction is used.
    rws.query("SET SESSION TRANSACTION READ ONLY");
    slave_trx("START TRANSACTION");
    slave_trx("START TRANSACTION");
    master_trx("START TRANSACTION READ WRITE");
    master_trx("START TRANSACTION READ WRITE");

    // Setting the access mode to READ WRITE while the session default is READ ONLY should cause the next
    // transaction to be routed to the master server.
    rws.query("SET TRANSACTION READ WRITE");
    master_trx("START TRANSACTION");
    slave_trx("START TRANSACTION");

    // Changing the default back to READ WRITE should make transactions behave normally.
    rws.query("SET SESSION TRANSACTION READ WRITE");
    master_trx("START TRANSACTION");

    // SET TRANSACTION READ ONLY should now again only redirect one transaction.
    rws.query("SET TRANSACTION READ ONLY");
    slave_trx("START TRANSACTION");
    master_trx("START TRANSACTION");

    return test.global_result;
}
