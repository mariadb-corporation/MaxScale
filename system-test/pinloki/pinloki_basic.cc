#include <maxtest/testconnections.hh>
#include "test_base.hh"

class BasicTest : public TestCase
{
public:
    using TestCase::TestCase;

    void pre() override
    {
        // Create a table with one row
        test.expect(master.query("CREATE TABLE test.t1(id INT)"), "CREATE failed: %s", master.error());
        test.expect(master.query("INSERT INTO test.t1 VALUES (1)"), "INSERT failed: %s", master.error());
        test.expect(master.query("FLUSH LOGS"), "FLUSH failed: %s", master.error());
        test.expect(master.query("CREATE TABLE test.t2 (id INT)"), "CREATE failed: %s", master.error());
        test.expect(master.query("INSERT INTO test.t2 VALUES (1)"), "INSERT failed: %s", master.error());
        sync_all();
    }

    void run() override
    {
        // test.t1 should contain one row
        auto result = slave.field("SELECT COUNT(*) FROM test.t1");
        test.expect(result == "1", "`test`.`t1` should have one row.");

        result = slave.field("SELECT COUNT(*) FROM test.t2");
        test.expect(result == "1", "`test`.`t2` should have one row.");

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
    return BasicTest(test).result();
}
