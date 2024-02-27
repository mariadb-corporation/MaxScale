/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-02-27
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxtest/testconnections.hh>
#include <maxbase/stopwatch.hh>
#include "test_base.hh"

class PurgeTest : public TestCase
{
public:
    using TestCase::TestCase;

    void create_new_logs(int num)
    {
        for (int i = 0; i < num; ++i)
        {
            master.query("FLUSH LOGS");
        }
        sync(master, maxscale);
    }

    void verify_logs(std::vector<std::string> expected_files,
                     const std::vector<std::string>& unexpected_files)
    {
        auto new_logs = maxscale.rows("SHOW BINARY LOGS");
        test.expect(new_logs.size() == expected_files.size(),
                    "Expected binary logs %s:\ndiffer from SHOW BINARY LOGS %s",
                    maxbase::create_list_string(expected_files).c_str(),
                    maxscale.pretty_rows("SHOW BINARY LOGS").c_str());

        auto index = test.maxscale->ssh_output("cat /var/lib/maxscale/binlogs/binlog.index");
        test.expect(index.rc == 0, "binlog.index should exist");
        test.expect(!index.output.empty(), "binlog.index should not be empty");

        auto files = mxb::strtok(index.output, "\n");
        test.expect(files.size() == expected_files.size(),
                    "Expected binary logs %s:\ndiffer from files in binlog.index %s",
                    maxbase::create_list_string(expected_files).c_str(),
                    maxbase::create_list_string(files).c_str());

        if (files.empty())
        {
            return;
        }

        auto pos = files[0].find_last_of('/');
        auto filepath = files[0].substr(0, pos + 1);

        // add the path to the expected files
        for (auto& expected_file : expected_files)
        {
            expected_file = filepath + expected_file;
        }

        std::sort(begin(expected_files), end(expected_files));
        std::sort(begin(files), end(files));

        test.expect(expected_files == files,
                    "Expected binary logs %s:\ndiffer from files in binlog.index %s",
                    maxbase::create_list_string(expected_files).c_str(),
                    maxbase::create_list_string(files).c_str()
                    );

        // Finally make sure the original files have been deleted
        for (const auto& a : unexpected_files)
        {
            auto file = test.maxscale->ssh_output("test -f " + filepath + a);
            test.expect(file.rc != 0, "File '%s' should not exist.", a.c_str());
        }
    }

    void test_purge()
    {
        create_new_logs(5);

        auto old_logs = maxscale.rows("SHOW BINARY LOGS");
        test.expect(!old_logs.empty(), "Empty reply to SHOW BINARY LOGS");
        auto log_to_keep = old_logs.back()[0];
        old_logs.pop_back();    // Keep these around so that we can check that they don't exist
        maxscale.query("PURGE BINARY LOGS TO '" + log_to_keep + "'");

        // Small delay here, because the binlog.index file is created via inotify
        std::this_thread::sleep_for(100ms);

        std::vector<std::string> unexpected_files;
        for (const auto& old : old_logs)
        {
            unexpected_files.push_back(old[0]);
        }

        verify_logs({log_to_keep}, unexpected_files);
    }

    void test_log_expiration()
    {
        // These should match the config
        const int expire_log_minimum_files = 2;
        const maxbase::Duration expire_log_duration = 30s;
        const maxbase::Duration purge_poll_timeout = 10s;
        const maxbase::Duration max_wait_time = expire_log_duration + purge_poll_timeout + 5s;

        const int num_new_logs = 10;

        create_new_logs(num_new_logs);

        maxbase::Timer timer(expire_log_duration);
        maxbase::StopWatch stop_watch;

        auto all_logs = maxscale.rows("SHOW BINARY LOGS");
        test.expect(all_logs.size() > num_new_logs, "Too few logs from SHOW BINARY LOGS");

        timer.wait_alarm();         // wait until the first moment logs could be purged
        timer = maxbase::Timer(1s); // then check once a second

        // Wait until the logs are purged, or until they should have been purged
        while (stop_watch.split() < max_wait_time)
        {
            auto new_logs = maxscale.rows("SHOW BINARY LOGS");
            if (new_logs.size() == expire_log_minimum_files)
            {
                break;
            }
            timer.wait_alarm();
        }

        std::vector<std::string> expected_files;
        for (int i = 0; i < expire_log_minimum_files; ++i)
        {
            expected_files.push_back(all_logs.back()[0]);
            all_logs.pop_back();
        }

        std::vector<std::string> unexpected_files;
        for (const auto& old : all_logs)
        {
            unexpected_files.push_back(old[0]);
        }

        verify_logs(expected_files, unexpected_files);
    }

    void run() override
    {
        test_purge();
        test_log_expiration();
    }
};

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);
    return PurgeTest(test).result();
}
