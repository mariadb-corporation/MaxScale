#include <maxtest/testconnections.hh>
#include "test_base.hh"

using namespace std::literals::string_literals;

class TlsTest : public TestCase
{
public:
    using TestCase::TestCase;

    void setup() override
    {
        master.ssl(true);
        slave.ssl(true);
        test.expect(maxscale.connect(), "Pinloki connection should work: %s", maxscale.error());
        test.expect(master.connect(), "Master connection should work: %s", master.error());
        test.expect(slave.connect(), "Slave connection should work: %s", slave.error());

        slave.query("STOP SLAVE; RESET SLAVE ALL;");

        auto change_master = change_master_sql(test.repl->ip(0), test.repl->port[0]);
        change_master += ", MASTER_SSL=1, MASTER_SSL_CA='"s
            + test.maxscales->access_homedir()
            + "/certs/ca.pem'";

        test.expect(maxscale.query(change_master), "CHANGE MASTER failed: %s", maxscale.error());
        test.expect(maxscale.query("START SLAVE"), "START SLAVE failed: %s", maxscale.error());
        sync(master, maxscale);

        slave.query(change_master_sql(test.maxscales->ip(), test.maxscales->rwsplit_port[0]));
        slave.query("START SLAVE");
        sync(maxscale, slave);
    }

    void run() override
    {
        test.expect(master.query("CREATE TABLE test.t1(id INT)"), "CREATE failed: %s", maxscale.error());
        test.expect(master.query("INSERT INTO test.t1 VALUES(1)"), "INSERT failed: %s", maxscale.error());
        test.expect(master.query("DROP TABLE test.t1"), "DROP failed: %s", maxscale.error());
        sync_all();
        check_gtid();
    }
};

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);
    return TlsTest(test).result();
}
