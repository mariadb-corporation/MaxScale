#include <maxtest/testconnections.hh>
#include "test_base.hh"

class StartStopTest : public TestCase
{
public:
    using TestCase::TestCase;

    void pre() override
    {
        test.expect(master.query("CREATE TABLE test.t1(id INT)"), "CREATE failed: %s", master.error());
        sync_all();
    }

    void run() override
    {
        for (int i = 0; i < 100 && test.ok(); i++)
        {
            test.expect(master.query("INSERT INTO test.t1 VALUES (" + std::to_string(i) + ")"),
                        "INSERT failed: %s", master.error());
            test.expect(maxscale.query("STOP SLAVE"), "STOP SLAVE failed: %s", maxscale.error());
            test.expect(maxscale.query("START SLAVE"), "START SLAVE failed: %s", maxscale.error());
        }

        sync_all();

        // All servers should be at the same GTID
        check_gtid();
    }

    void post() override
    {
        test.expect(master.query("DROP TABLE test.t1"), "DROP failed: %s", master.error());
    }

private:
};

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);
    return StartStopTest(test).result();
}
