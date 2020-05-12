#include <testconnections.h>
#include "test_base.hh"

class FileTest : public TestCase
{
public:
    using TestCase::TestCase;

    void pre() override
    {
        master.query("FLUSH LOGS");
        master.query("FLUSH LOGS");
        master.query("FLUSH LOGS");
        master.query("FLUSH LOGS");
        master.query("FLUSH LOGS");
        sync(master, maxscale);
    }

    void run() override
    {
        auto orig = master.pretty_rows("SHOW BINARY LOGS");
        auto mxs = maxscale.pretty_rows("SHOW BINARY LOGS");
        test.expect(mxs == orig,
                    "SHOW BINARY LOGS should return the same result:\n"
                    "Master:\n%s\nMaxScale:\n%s",
                    orig.c_str(), mxs.c_str());

        auto index = test.maxscales->ssh_output("cat /var/lib/maxscale/binlogs/binlog.index");
        test.expect(index.first == 0, "binlog.index should exist");
        test.expect(!index.second.empty(), "binlog.index should not be empty");

        for (const auto& a : mxb::strtok(index.second, "\n"))
        {
            auto file = test.maxscales->ssh_output("test -f " + a);
            test.expect(file.first == 0, "File '%s' does not exist.", a.c_str());
        }
    }

private:
};

int main(int argc, char** argv)
{
    Mariadb_nodes::require_gtid(true);
    TestConnections test(argc, argv);
    return FileTest(test).result();
}
