#include <maxtest/testconnections.hh>
#include "test_base.hh"

class GtidTest : public TestCase
{
public:
    using TestCase::TestCase;

    void run() override
    {
        for (int i = 0; i < 5 && test.ok(); i++)
        {
            test.tprintf("Test %d", i + 1);
            run_test();
        }
    }

private:
    void run_test()
    {
        test.tprintf("Create table and replicate it");
        test.expect(master.query("CREATE TABLE test.t1(id INT)"), "CREATE failed: %s", master.error());
        sync_all();

        test.tprintf("Stop replication on the slave and MaxScale");
        maxscale.query("STOP SLAVE");
        slave.query("STOP SLAVE");

        test.tprintf("Insert the first batch of data and record the GTID position");
        for (int j = 0; j < 5; j++)
        {
            master.query("INSERT INTO test.t1 VALUES (" + std::to_string(j) + ")");
        }

        auto gtid_pos = master.field("SELECT @@gtid_current_pos");

        test.tprintf("Insert the more data and start replicating from GTID '%s'", gtid_pos.c_str());
        for (int j = 0; j < 5; j++)
        {
            master.query("INSERT INTO test.t1 VALUES (" + std::to_string(j) + ")");
        }

        test.tprintf("Set MaxScale GTID position");
        maxscale.query("SET GLOBAL gtid_slave_pos='" + gtid_pos + "'");
        test.tprintf("START SLAVE on MaxScale");
        maxscale.query("START SLAVE");
        test.tprintf("Sync MaxScale");
        sync(master, maxscale);

        // Note that we don't set gtid_slave_pos like we do on MaxScale. This is not done as gtid_pos is not
        // in the binlogs that are on MaxScale and would be treated as an error.
        slave.query("START SLAVE");
        test.tprintf("Sync slave to '%s', currently at '%s'",
                     maxscale.field("SELECT @@gtid_slave_pos").c_str(),
                     gtid_pos.c_str());
        sync(maxscale, slave);

        auto master_rows = master.field("SELECT COUNT(*) FROM test.t1");
        auto slave_rows = slave.field("SELECT COUNT(*) FROM test.t1");

        test.expect(master_rows == "10", "Master should have ten rows: %s", master_rows.c_str());
        test.expect(slave_rows == "5", "Slave should have five rows: %s", slave_rows.c_str());
        check_gtid();

        test.expect(master.query("DROP TABLE test.t1"), "DROP failed: %s", master.error());
    }
};

int main(int argc, char** argv)
{
    MariaDBCluster::require_gtid(true);
    TestConnections test(argc, argv);
    return GtidTest(test).result();
}
