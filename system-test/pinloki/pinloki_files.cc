#include <maxtest/testconnections.hh>
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
        auto orig = master.rows("SHOW BINARY LOGS");
        auto mxs = maxscale.rows("SHOW BINARY LOGS");

        for (size_t i = 0; i < orig.size() && i < mxs.size(); i++)
        {
            test.expect(mxs[i][0] == orig[i][0],
                        "SHOW BINARY LOGS should return the same result:\n"
                        "Master:\n%s\nMaxScale:\n%s",
                        orig[i][0].c_str(), mxs[i][0].c_str());
        }

        auto index = test.maxscale->ssh_output("cat /var/lib/maxscale/binlogs/binlog.index");
        test.expect(index.rc == 0, "binlog.index should exist");
        test.expect(!index.output.empty(), "binlog.index should not be empty");

        for (const auto& a : mxb::strtok(index.output, "\n"))
        {
            auto file = test.maxscale->ssh_output("test -f " + a);
            test.expect(file.rc == 0, "File '%s' does not exist.", a.c_str());
        }
    }

private:
};

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);
    return FileTest(test).result();
}
