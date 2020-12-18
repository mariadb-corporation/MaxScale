#include "test_base.hh"
#include <iostream>
#include <iomanip>


// This test is a combination of pinloki/test_base.hh and galera_priority.cpp.

void check_server_id(TestConnections& test, const std::string& id)
{
    test.tprintf("Expecting '%s'...", id.c_str());
    auto conn = test.maxscales->rwsplit();
    test.expect(conn.connect(), "Connection should work: %s", conn.error());
    test.expect(conn.query("BEGIN"), "BEGIN should work: %s", conn.error());
    auto f = conn.field("SELECT @@server_id");
    test.expect(f == id, "Expected server_id '%s', not server_id '%s'", id.c_str(), f.c_str());
    test.expect(conn.query("COMMIT"), "COMMIT should work: %s", conn.error());
}

std::string replicating_from(Connection& conn)
{
    std::string addr;

    const auto& rows = conn.rows("SHOW SLAVE STATUS");
    if (!rows.empty() && rows[0].size() >= 2)
    {
        addr = rows[0][1];
    }

    return addr;
}

void check_table(TestConnections& test, Connection& conn, int n)
{
    auto result = conn.field("SELECT COUNT(*) FROM test.t1");
    int m = atoi(result.c_str());
    test.expect(n == m, "test.t1 should have %d rows, but has %d rows.", n, m);
}

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);
    test.require_galera(true);
    test.galera->connect();
    auto galera_ids = test.galera->get_all_server_ids_str();

    Connection pinloki = test.maxscales->readconn_master();
    test.expect(pinloki.connect(), "Pinloki connection should work: %s", pinloki.error());

    // Pick a regular replica and make it replicate from pinloki
    Connection pinloki_replica {test.repl->get_connection(2)};
    test.expect(pinloki_replica.connect(), "Regular replica connection should work: %s",
                pinloki_replica.error());

    std::cout << "pinloki_replica " << pinloki_replica.host() << std::endl;

    // and make it replicate from pinloki.
    pinloki_replica.query("STOP SLAVE");
    pinloki_replica.query("RESET SLAVE");
    pinloki_replica.query(change_master_sql(pinloki.host().c_str(), pinloki.port()));
    pinloki_replica.query("START SLAVE");

    // Create a table via RWS (galera cluster) and insert one value
    Connection rws = test.maxscales->rwsplit();
    test.expect(rws.connect(), "RWS connection should work: %s", rws.error());
    rws.query("DROP TABLE if exists test.t1");
    test.expect(rws.query("CREATE TABLE test.t1(id INT)"), "CREATE failed: %s", rws.error());
    test.expect(rws.query("INSERT INTO test.t1 values(1)"), "Insert 1 failed: %s", rws.error());

    // Now check that things are as exptected
    // Galera node 3 should be master,
    check_server_id(test, galera_ids[2]);
    // and pinloki should be replicating from it.
    auto pin_repl_from = replicating_from(pinloki);
    test.expect(pin_repl_from == test.galera->ip(2), "Pinloki should replicate from the galera node 3");
    // and the pinloki_replica should replicate from pinloki
    auto reg_repl_from = replicating_from(pinloki_replica);
    test.expect(reg_repl_from == pinloki.host().c_str(), "pinloki_replica should replicate from pinloki");
    // and reading test.t1 from pinloki_replica should have 1 row
    // check_table(test, pinloki_replica, 1); This fails because galera is not in gtid mode

    std::cout << "replicating_from(pinloki) = " << replicating_from(pinloki) << std::endl;
    std::cout << "replicating_from(pinloki_replica) = " << replicating_from(pinloki_replica) << std::endl;

    /** Block node 3 and node 1 should be master */
    test.galera->block_node(2);
    test.maxscales->wait_for_monitor(2);
    check_server_id(test, galera_ids[0]);

    pin_repl_from = replicating_from(pinloki);
    test.expect(pin_repl_from == test.galera->ip(0), "Pinloki should replicate from the galera node 0");

    std::cout << "replicating_from(pinloki) = " << replicating_from(pinloki) << std::endl;
    std::cout << "replicating_from(pinloki_replica) = " << replicating_from(pinloki_replica) << std::endl;

    return test.global_result;
}
