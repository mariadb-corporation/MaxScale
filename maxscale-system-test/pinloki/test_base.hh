#include <testconnections.h>

// Base class for Pinloki test cases. Provides some utility functions related to replication as well as common
// testing functionality.
class TestCase
{
public:
    TestCase(TestConnections& t)
        : test(t)
        , master(test.repl->get_connection(0))
        , slave(test.repl->get_connection(1))
        , maxscale(test.maxscales->rwsplit())
    {
    }

    // Runs the test and returns the result code (0 for no errors)
    int result()
    {
        setup();
        pre();

        if (test.ok())
        {
            run();
            post();
        }

        teardown();
        return test.global_result;
    }

protected:
    //
    // The following should be overridden by the test case
    //

    // The actual test
    virtual void run() = 0;

    // Any steps needed to be done before the test
    virtual void pre()
    {
    }

    // Any cleanup that needs to be done that was done in `pre`
    virtual void post()
    {
    }

    // Test setup. Connects all Connections and sets up replication between
    // the master, MaxScale and a slave. Only override if custom test setup is needed.
    virtual void setup()
    {
        test.set_timeout(60);
        test.expect(maxscale.connect(), "Pinloki connection should work: %s", maxscale.error());
        test.expect(master.connect(), "Master connection should work: %s", master.error());
        test.expect(slave.connect(), "Slave connection should work: %s", slave.error());

        // Stop the slave while we configure pinloki
        slave.query("STOP SLAVE; RESET SLAVE ALL;");

        // Start replicating from the master
        maxscale.query("STOP SLAVE");
        maxscale.query("RESET SLAVE");
        maxscale.query(change_master_sql(test.repl->ip(0), test.repl->port[0]));
        maxscale.query("START SLAVE");

        // Sync MaxScale with the master
        test.set_timeout(60);
        sync(master, maxscale);

        // Configure the slave to replicate from MaxScale and sync it
        test.set_timeout(60);
        slave.query(change_master_sql(test.maxscales->ip(0), test.maxscales->rwsplit_port[0]));
        slave.query("START SLAVE");
        sync(maxscale, slave);
        test.stop_timeout();
    }

    // Test teardown, fixes replication. Override if `setup` was overridden.
    virtual void teardown()
    {
        test.repl->fix_replication();
    }

protected:
    TestConnections& test;      // The core test library
    Connection       master;    // Connection to the master
    Connection       slave;     // Connection to the slave
    Connection       maxscale;  // Connection to MaxScale

    // Syncs the `dest` connection with the `src` connection
    void sync(Connection& src, Connection& dest)
    {
        test.set_timeout(130);
        auto gtid = src.field("SELECT @@gtid_current_pos");
        auto start_gtid = dest.field("SELECT @@gtid_current_pos");
        auto res = dest.field("SELECT MASTER_GTID_WAIT('" + gtid + "', 120)");
        test.expect(res == "0",
                    "`MASTER_GTID_WAIT('%s', 120)` returned: %s (error: %s). "
                    "Target GTID: %s Starting GTID: %s",
                    gtid.c_str(), res.c_str(), slave.error(), gtid.c_str(), start_gtid.c_str());
    }

    // Checks that `master`, `maxscale` and `slave` all report the same GTID position
    void check_gtid()
    {
        auto master_pos = master.field("SELECT @@gtid_current_pos");
        auto slave_pos = slave.field("SELECT @@gtid_current_pos");
        auto maxscale_pos = maxscale.field("SELECT @@gtid_current_pos");

        test.expect(maxscale_pos == master_pos,
                    "MaxScale GTID (%s) is not the same as Master GTID (%s)",
                    maxscale_pos.c_str(), master_pos.c_str());

        test.expect(slave_pos == maxscale_pos,
                    "Slave GTID (%s) is not the same as MaxScale GTID (%s)",
                    slave_pos.c_str(), maxscale_pos.c_str());
    }

    std::string change_master_sql(const char* host, int port,
                                  const char* user = "maxskysql",
                                  const char* password = "skysql")
    {
        std::ostringstream ss;

        ss << "CHANGE MASTER TO MASTER_HOST='" << host << "', MASTER_PORT=" << port
           << ", MASTER_USER='" << user << "', MASTER_PASSWORD='" << password
           << "', MASTER_USE_GTID=SLAVE_POS";

        return ss.str();
    }
};
