#include <maxtest/testconnections.hh>
#include "test_base.hh"

class ChangeMasterTest : public TestCase
{
public:
    using TestCase::TestCase;

    void pre() override
    {
        master.query("CREATE TABLE test.t1(id INT)");
    }

    void run() override
    {
        for (int i = 0; i < 5 && test.ok(); i++)
        {
            swap_master();
        }
    }


    void post() override
    {
        master.query("DROP TABLE test.t1");
    }

private:
    void swap_master()
    {
        test.tprintf("Check that starting setup works");
        check(master, slave);

        test.tprintf("Stop slave on promoted slave");
        slave.query("STOP SLAVE");
        test.tprintf("Flush logs until the promoted slave is ahead of the master");
        flush_until_ahead(slave, master.field("SHOW MASTER STATUS"));

        test.tprintf("Point MaxScale to it");
        maxscale.query("STOP SLAVE");
        maxscale.query(change_master_sql(test.repl->ip(1), test.repl->port[1]));
        maxscale.query("START SLAVE");

        test.tprintf("Point demoted master to maxscale");
        master.query(change_master_sql(test.maxscales->ip(0), test.maxscales->rwsplit_port[0],
                                       GtidPos::CURRENT));
        master.query("START SLAVE");

        test.tprintf("Check that new setup works");
        check(slave, master);

        test.tprintf("Stop slave on demoted master");
        master.query("STOP SLAVE");
        test.tprintf("Flush logs until the demoted master is ahead of the promoted slave");
        flush_until_ahead(master, slave.field("SHOW MASTER STATUS"));

        test.tprintf("Point MaxScale to the original master");
        maxscale.query("STOP SLAVE");
        maxscale.query(change_master_sql(test.repl->ip(0), test.repl->port[0]));
        maxscale.query("START SLAVE");

        test.tprintf("Point original slave back at MaxScale");
        slave.query(change_master_sql(test.maxscales->ip(0), test.maxscales->rwsplit_port[0],
                                      GtidPos::CURRENT));
        slave.query("START SLAVE");

        test.tprintf("Check that resulting setup works");
        check(master, slave);
    }

    void check(Connection& m, Connection& s)
    {
        m.query("INSERT INTO test.t1 VALUES (1)");
        sync(m, maxscale);
        sync(maxscale, s);

        auto master_rows = m.field("SELECT COUNT(*) FROM test.t1");
        auto slave_rows = s.field("SELECT COUNT(*) FROM test.t1");

        test.expect(master_rows == slave_rows,
                    "Expected slave to have %s rows but it was %s",
                    master_rows.c_str(), slave_rows.c_str());

        check_gtid();
    }

    void flush_until_ahead(Connection& c, const std::string& current_binlog)
    {
        int target = atoi(current_binlog.substr(current_binlog.find_last_of('.') + 1).c_str());
        auto binlog = c.field("SHOW MASTER STATUS");

        while (atoi(binlog.substr(binlog.find_last_of('.') + 1).c_str()) <= target)
        {
            c.query("FLUSH LOGS");
            binlog = c.field("SHOW MASTER STATUS");
        }
    }
};

int main(int argc, char** argv)
{
    MariaDBCluster::require_gtid(true);
    TestConnections test(argc, argv);
    return ChangeMasterTest(test).result();
}
