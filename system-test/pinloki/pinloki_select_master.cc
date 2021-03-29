#include <maxtest/testconnections.hh>
#include "test_base.hh"

class MasterSelectTest : public TestCase
{
public:
    using TestCase::TestCase;

    void setup() override
    {
        test.expect(maxscale.connect(), "Pinloki connection should work: %s", maxscale.error());
        test.expect(master.connect(), "Master connection should work: %s", master.error());
        test.expect(slave.connect(), "Slave connection should work: %s", slave.error());

        sync(master, maxscale);

        slave.query("STOP SLAVE; RESET SLAVE ALL;");
        slave.query(change_master_sql(test.maxscales->ip(0), test.maxscales->rwsplit_port[0]));
        slave.query("START SLAVE");
        sync(maxscale, slave);
    }

    void run() override
    {
        test.expect(!maxscale.query(change_master_sql(test.repl->ip(0), test.repl->port[0])),
                    "CHANGE MASTER should fail");
        test.expect(maxscale.query("STOP SLAVE"), "STOP SLAVE should work: %s", maxscale.error());
        test.expect(maxscale.query("START SLAVE"), "START SLAVE should work: %s", maxscale.error());

        check_gtid();

        test.expect(master.query("CREATE TABLE test.t1(id INT)"), "CREATE failed: %s", master.error());
        test.expect(master.query("INSERT INTO test.t1 VALUES (1)"), "INSERT failed: %s", master.error());
        test.expect(master.query("DROP TABLE test.t1"), "DROP failed: %s", master.error());

        sync(master, maxscale);
        sync(maxscale, slave);

        check_gtid();
    }
};

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);
    return MasterSelectTest(test).result();
}
