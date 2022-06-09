#include <maxtest/testconnections.hh>
#include "test_base.hh"

std::string case1 = "Earth is the third planet";
std::string case2 = "Mars is the fourth planet";
std::string case3 = "Jupiter is the fifth planet";
std::string case4 = "Saturn is the sixth planet";
std::string case5 = "Uranus is the seventh planet";

class EncryptionTest : public TestCase
{
public:
    using TestCase::TestCase;

    void query(std::string query)
    {
        test.expect(master.query(query), "Query '%s' failed: %s", query.c_str(), master.error());
    }

    void check_contents(std::string tbl, std::string str)
    {
        auto result = slave.field("SELECT a FROM " + tbl + " WHERE a = '" + str + "'");
        test.expect(result == str,
                    "%s should have a row with '%s' in it.",
                    tbl.c_str(), str.c_str());
    }

    void check_encryption(std::initializer_list<std::string> strs)
    {
        auto rv = test.maxscale->ssh_output("find /var/lib/maxscale/binlogs/ -type f -exec strings {} \\;");

        for (const auto& str : strs)
        {
            test.expect(rv.output.find(str) == std::string::npos,
                        "'%s' should not be visible in the binlogs: %s",
                        str.c_str(), rv.output.c_str());
        }
    }

    void setup() override
    {
        setup_select_master();
    }

    void pre() override
    {
        query("CREATE TABLE test.t1(a VARCHAR(255))");
        query("INSERT INTO test.t1 VALUES ('" + case1 + "')");
        query("FLUSH LOGS");
        query("CREATE TABLE test.t2 (a TEXT)");
        query("INSERT INTO test.t2 VALUES ('" + case1 + "')");
        sync_all();
    }

    void run() override
    {
        test.tprintf("Sanity check");
        check_contents("test.t1", case1);
        check_contents("test.t2", case1);
        check_gtid();

        // Restart MaxScale and insert new values. Old values should not be visible.
        test.maxscale->restart();

        query("INSERT INTO test.t1 VALUES ('" + case2 + "')");
        query("INSERT INTO test.t2 VALUES ('" + case3 + "')");

        test.tprintf("Encryption after restart");
        // Reconnect to MaxScale since it was restarted and force the slave to reconnect as well.
        maxscale.connect();
        slave.query("STOP SLAVE; START SLAVE;");
        sync_all();

        check_contents("test.t1", case2);
        check_contents("test.t2", case3);

        test.tprintf("Key rotation");
        test.check_maxctrl("rotate encryption");

        query("INSERT INTO test.t1 VALUES ('" + case4 + "')");
        sync_all();
        check_contents("test.t1", case4);

        test.tprintf("Key reloading");
        test.check_maxctrl("reload encryption");

        check_encryption({case1, case2, case3, case4, case5});
    }

    void post() override
    {
        test.expect(master.query("DROP TABLE test.t1"), "DROP failed: %s", master.error());
        test.expect(master.query("DROP TABLE test.t2"), "DROP failed: %s", master.error());
    }
};

int main(int argc, char** argv)
{
    TestConnections::skip_maxscale_start(true);
    TestConnections test(argc, argv);

    // Create the encryption key before MaxScale is started
    test.maxscale->ssh_node_f(true, "(echo -n '1;'; openssl rand -hex 32)|cat > /tmp/encryption.key");
    test.maxscale->start();

    return EncryptionTest(test).result();
}
