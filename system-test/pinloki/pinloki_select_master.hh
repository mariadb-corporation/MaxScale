#include <maxtest/testconnections.hh>
#include "test_base.hh"

class MasterSelectTest : public TestCase
{
public:
    using TestCase::TestCase;

    void setup() override
    {
        setup_select_master();
    }

    void run() override
    {
        test.expect(!maxscale.query(change_master_sql(test.repl->ip(0), test.repl->port(0))),
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
