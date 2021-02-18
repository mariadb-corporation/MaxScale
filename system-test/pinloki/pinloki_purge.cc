#include <maxtest/testconnections.hh>
#include "test_base.hh"

class PurgeTest : public TestCase
{
public:
    using TestCase::TestCase;

    void run() override
    {
        // Make sure we have multiple binlogs
        master.query("FLUSH LOGS");
        master.query("FLUSH LOGS");
        master.query("FLUSH LOGS");
        master.query("FLUSH LOGS");
        sync(master, maxscale);

        auto old_logs = maxscale.rows("SHOW BINARY LOGS");
        test.expect(!old_logs.empty(), "Empty reply to SHOW BINARY LOGS");
        auto log_to_keep = old_logs.back()[0];
        old_logs.pop_back();    // Keep these around so that we can check that they don't exist
        maxscale.query("PURGE BINARY LOGS TO '" + log_to_keep + "'");

        auto new_logs = maxscale.rows("SHOW BINARY LOGS");
        test.expect(new_logs.size() == 1, "All but the latest binlog should be purged:\n%s",
                    maxscale.pretty_rows("SHOW BINARY LOGS").c_str());

        auto index = test.maxscales->ssh_output("cat /var/lib/maxscale/binlogs/binlog.index");
        test.expect(index.rc == 0, "binlog.index should exist");
        test.expect(!index.output.empty(), "binlog.index should not be empty");

        auto files = mxb::strtok(index.output, "\n");
        test.expect(files.size() == 1, "Only the latest binlog should be listed");

        auto pos = files[0].find_last_of('/');
        auto filename = files[0].substr(pos + 1);
        test.expect(filename == log_to_keep, "Only the requested log should exist");

        auto filepath = files[0].substr(0, pos);

        for (const auto& a : old_logs)
        {
            auto file = test.maxscales->ssh_output("test -f " + filepath + "/" + a[0]);
            test.expect(file.rc != 0, "File '%s' should not exist.", a[0].c_str());
        }
    }
};

int main(int argc, char** argv)
{
    MariaDBCluster::require_gtid(true);
    TestConnections test(argc, argv);
    return PurgeTest(test).result();
}
