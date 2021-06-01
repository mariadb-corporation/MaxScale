#include "test_base.hh"
#include <iostream>
#include <iomanip>

namespace
{
std::string replicating_from(Connection& conn)
{
    const auto& rows = conn.rows("SHOW SLAVE STATUS");
    return rows[0][1];
}
}

class UpgradeTest : public TestCase
{
public:
    using TestCase::TestCase;

    void pre() override
    {
        master.query("CREATE TABLE test.data(id INT)");
    }

    void run() override
    {
        upgrade();
    }

    void post() override
    {
        master.query("DROP TABLE test.data");
    }

private:
    int insert(int begin, int n)
    {
        int end = begin + n;
        for (int i = begin; i != end; ++i)
        {
            master.query("INSERT INTO test.data VALUES (" + std::to_string(i) + ")");
        }
        return end;
    }

    void upgrade()
    {
        char buf[1024];
        int ninserts = 0;

        // Create data that represents the situation from before pinloki existed.
        test.tprintf("Create data for the \"old system\".");
        ninserts = insert(ninserts, 10);
        master.query("FLUSH LOGS");     // create a few extra logs
        master.query("FLUSH LOGS");     // create a few extra logs
        ninserts = insert(ninserts, 10);
        sync(master, slave);

        int org_log_count = maxscale.rows("SHOW BINARY LOGS").size();
        test.expect(org_log_count >= 3, "maxscale should have at least 3 logs");

        // Latest gtid
        auto gtid_pos = slave.field("SELECT @@gtid_slave_pos");
        test.tprintf("Gtid pos of \"old system\" %s", gtid_pos.c_str());

        // Stop the slave and maxscale, remove the binlog data
        test.tprintf("Stop maxscale and its slave. Remove binlog data.");

        slave.query("STOP SLAVE");
        test.maxscales->stop_and_check_stopped();
        auto res = test.maxscales->ssh_output("rm -rf /var/lib/maxscale/binlogs");
        test.expect(res.rc == 0, "Failed to remove binlog data %s",
                    strerror_r(res.rc, buf, sizeof(buf)));

        // Purge all but the latest log from the master
        auto logs = master.rows("SHOW BINARY LOGS");
        master.query("PURGE BINARY LOGS TO '" + logs.back()[0] + "'");

        test.tprintf("\"old system\" neutered. Restart and wait for ReplSYNC.");

        // Bring maxscale up, and start the slave.
        test.maxscales->start_and_check_started();
        maxscale = Connection(test.maxscales->rwsplit());
        test.expect(maxscale.connect(), "Pinloki connection should work: %s", maxscale.error());

        maxscale.query(change_master_sql(test.repl->ip(0), test.repl->port[0]));
        maxscale.query("START SLAVE");
        slave.query("START SLAVE");     // making sure the slave can be connected before sync

        sleep(12);      // It takes 10 seconds before pinloki starts reporting
        test.tprintf("Check for ReplSYNC.");

        // The slave should be connected and Reader waiting for Writer to sync
        test.log_includes("ReplSYNC: Reader waiting for primary to sync.");

        // Maxscale should not receive any binlog data yet
        int zero_count = maxscale.rows("SHOW BINARY LOGS").size();
        test.expect(zero_count == 0, "maxscale should not have any binary logs");

        // Tell pinloki where to start. Start the Writer.
        maxscale.query("STOP SLAVE");
        maxscale.query("SET GLOBAL gtid_slave_pos='" + gtid_pos + "'");
        maxscale.query("START SLAVE");

        // Highjack another slave to replicate from maxscale
        Connection slave2 {test.repl->get_connection(2)};
        slave2.connect();
        slave2.query("STOP SLAVE");
        slave2.query(change_master_sql(maxscale.host().c_str(), maxscale.port(), GtidPos::CURRENT));
        slave2.query("START SLAVE");

        sync(master, slave);    // sync master => pinloki => slave
        sync(master, slave2);   // sync master => pinloki => slave2

        // Check that the master->pinloki->slave replication works
        ninserts = insert(ninserts, 10);
        sync_all();
        sleep(5); // TODO: Check sync_all(), this sleep should not be needed.
        auto master_row_count = std::stoi(master.field("SELECT COUNT(*) FROM test.data"));
        auto slave_row_count = std::stoi(slave.field("SELECT COUNT(*) FROM test.data"));
        auto slave2_row_count = std::stoi(slave2.field("SELECT COUNT(*) FROM test.data"));

        test.expect(master_row_count == ninserts,
                    "Master row count does not match ninserts = %d", ninserts);

        test.expect(master_row_count == slave_row_count,
                    "Master row count %d does not match slave row count %d",
                    master_row_count, slave_row_count);

        test.expect(master_row_count == slave2_row_count,
                    "Master row count %d does not match slave2 row count %d",
                    master_row_count, slave2_row_count);
    }
};

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);
    return UpgradeTest(test).result();
}
