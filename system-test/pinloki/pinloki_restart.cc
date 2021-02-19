#include <maxtest/testconnections.hh>
#include "test_base.hh"

class RestartTest : public TestCase
{
public:
    using TestCase::TestCase;

    void pre() override
    {
        master.query("CREATE TABLE test.t1(id INT)");
    }

    void run() override
    {
        for (int i = 0; i < 20 && test.ok(); i++)
        {
            master.query("INSERT INTO test.t1 VALUES (" + std::to_string(i) + ")");
            test.maxscales->restart();
            test.expect(maxscale.connect(), "Reconnection after restart should work");
            sync(master, maxscale);
        }

        // This makes sure the slave is actively replicating
        slave.query("STOP SLAVE");
        slave.query("START SLAVE");
        sync(maxscale, slave);

        auto master_rows = master.field("SELECT COUNT(*) FROM test.t1");
        auto slave_rows = slave.field("SELECT COUNT(*) FROM test.t1");

        test.expect(master_rows == slave_rows,
                    "Expected slave to have %s rows but it was %s",
                    master_rows.c_str(), slave_rows.c_str());

        check_gtid();
    }


    void post() override
    {
        master.query("DROP TABLE test.t1");
    }
};

int main(int argc, char** argv)
{
    MariaDBCluster::require_gtid(true);
    TestConnections test(argc, argv);
    return RestartTest(test).result();
}
