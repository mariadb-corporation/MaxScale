#include <testconnections.h>

std::string change_master_sql(const char* host, int port)
{
    const char* user = "maxskysql";
    const char* password = "skysql";
    std::ostringstream ss;

    ss << "CHANGE MASTER TO MASTER_HOST='" << host << "', MASTER_PORT=" << port
       << ", MASTER_USER='" << user << "', MASTER_PASSWORD='" << password << "', MASTER_USE_GTID=SLAVE_POS";

    return ss.str();
}

void sync_slave(Connection& master, Connection& slave)
{
    slave.field("SELECT MASTER_GTID_WAIT('" + master.field("SELECT @@gtid_current_pos") + "', 120)");
}

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);

    auto conn = test.maxscales->rwsplit();
    auto master = test.repl->get_connection(0);
    auto slave = test.repl->get_connection(1);
    test.expect(conn.connect(), "Pinloki connection should work: %s", conn.error());
    test.expect(master.connect(), "Master connection should work: %s", master.error());
    test.expect(slave.connect(), "Slave connection should work: %s", slave.error());

    // Stop the slave while we configure pinloki
    slave.query("STOP SLAVE; RESET SLAVE ALL;");

    // Insert some data
    test.expect(master.query("CREATE OR REPLACE TABLE test.t1(id INT)"),
                "CREATE should work: %s", master.error());
    test.expect(master.query("INSERT INTO test.t1 VALUES (1)"),
                "INSERT should work: %s", master.error());

    // Start replicating from the master
    conn.query(change_master_sql(test.repl->ip(0), test.repl->port[0]));
    conn.query("START SLAVE");

    // Sync MaxScale with the master
    test.set_timeout(60);
    sync_slave(master, conn);

    // Configure the slave to replicate from MaxScale and sync it
    test.set_timeout(60);
    slave.query(change_master_sql(test.maxscales->ip(0), test.maxscales->rwsplit_port[0]));
    slave.query("START SLAVE");
    sync_slave(conn, slave);

    // The end result should be that test.t1 contains one row and that all three servers are at the same GTID.
    auto result = slave.field("SELECT COUNT(*) FROM test.t1");
    test.expect(result == "1", "`test`.`t1` should have one row.");
    auto master_pos = master.field("SELECT @@gtid_current_pos");
    auto slave_pos = slave.field("SELECT @@gtid_current_pos");
    auto maxscale_pos = conn.field("SELECT @@gtid_current_pos");

    test.expect(maxscale_pos == master_pos,
                "MaxScale GTID (%s) is not the same as Master GTID (%s)",
                maxscale_pos.c_str(), master_pos.c_str());

    test.expect(slave_pos == maxscale_pos,
                "Slave GTID (%s) is not the same as MaxScale GTID (%s)",
                slave_pos.c_str(), maxscale_pos.c_str());

    master.query("DROP TABLE test.t1");

    test.repl->fix_replication();
    return test.global_result;
}
