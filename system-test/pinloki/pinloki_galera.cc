#include "test_base.hh"
#include <iostream>
#include <iomanip>
#include <maxtest/testconnections.hh>
#include <maxtest/galera_cluster.hh>

const size_t NUM_GALERAS = 4;

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

void block_galera_ip(TestConnections& test, const std::string& galera_ip)
{
    size_t i = 0;
    for (; i < NUM_GALERAS; ++i)
    {
        if (galera_ip == test.galera->ip(i))
        {
            break;
        }
    }

    if (i == NUM_GALERAS)
    {
        test.add_result(true, "Expected IP '%s' to be a galera node\n", galera_ip.c_str());
    }
    else
    {
        std::cout << "Blocking node " << i << " IP " << test.galera->ip(i) << std::endl;
        test.galera->block_node(i);
    }
}

void check_table(TestConnections& test, Connection& conn, int n)
{
    auto result = conn.field("SELECT COUNT(*) FROM test.t1");
    int m = atoi(result.c_str());
    test.expect(n == m, "test.t1 should have %d rows, but has %d rows.", n, m);
}

int main(int argc, char** argv)
{
    TestConnections::restart_galera(true);
    TestConnections test(argc, argv);
    test.galera->connect();
    auto galera_ids = test.galera->get_all_server_ids_str();

    Connection pinloki = test.maxscale->readconn_master();
    test.expect(pinloki.connect(), "Pinloki connection should work: %s", pinloki.error());

    // Pick a regular replica and make it replicate from pinloki
    Connection pinloki_replica {test.repl->get_connection(2)};
    test.expect(pinloki_replica.connect(), "Regular replica connection should work: %s",
                pinloki_replica.error());

    std::cout << "pinloki_replica " << pinloki_replica.host() << std::endl;

    // and make it replicate from pinloki.
    pinloki_replica.query("STOP SLAVE");
    pinloki_replica.query("RESET SLAVE");
    pinloki_replica.query("SET @@global.gtid_slave_pos = '0-101-1'");
    pinloki_replica.query(change_master_sql(pinloki.host().c_str(), pinloki.port()));
    pinloki_replica.query("START SLAVE");

    // Create a table via RWS (galera cluster) and insert one value
    Connection rws = test.maxscale->rwsplit();
    test.expect(rws.connect(), "RWS connection should work: %s", rws.error());
    rws.query("DROP TABLE if exists test.t1");
    test.expect(rws.query("CREATE TABLE test.t1(id INT)"), "CREATE failed: %s", rws.error());
    test.expect(rws.query("INSERT INTO test.t1 values(1)"), "INSERT 1 failed: %s", rws.error());

    sleep(5);

    // Check that things are as they should be.
    // The pinloki_replica should replicate from pinloki
    auto reg_repl_from = replicating_from(pinloki_replica);
    test.expect(reg_repl_from == pinloki.host().c_str(), "pinloki_replica should replicate from pinloki");

    // Reading test.t1 from pinloki_replica should have 1 row
    check_table(test, pinloki_replica, 1);

    auto pinloki_repl_from = replicating_from(pinloki);
    std::cout << "replicating_from(pinloki) = " << pinloki_repl_from << std::endl;
    std::cout << "replicating_from(pinloki_replica) = " << replicating_from(pinloki_replica) << std::endl;

    auto previous_ip = pinloki_repl_from;

    /** Block the node pinloki is replicating from */
    block_galera_ip(test, pinloki_repl_from);

    /** Make sure pinloki is now replicating from another node */
    for (int i = 0; i < 60; ++i)    // TODO, takes long, ~30s. What are the timeouts?
    {
        pinloki_repl_from = replicating_from(pinloki);
        std::cout << "replicating_from(pinloki) = " << pinloki_repl_from << std::endl;
        if (previous_ip != pinloki_repl_from)
        {
            break;
        }

        sleep(1);
    }

    test.expect(previous_ip != pinloki_repl_from,
                "pinloki should have started to replicate from another node");

    /** Insert and check */
    auto conn = test.maxscale->rwsplit();      // for some reason rws is no longer valid?
    test.expect(conn.connect(), "2nd RWS connection should work: %s", conn.error());
    test.expect(conn.query("INSERT INTO test.t1 values(2)"), "INSERT 2 failed: %s", conn.error());

    sleep(5);

    check_table(test, pinloki_replica, 2);

    return test.global_result;
}
