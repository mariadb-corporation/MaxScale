/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-04-03
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxtest/testconnections.hh>
#include <maxbase/stopwatch.hh>
#include "test_base.hh"
#include <iostream>

namespace
{
// These values must match the config
const size_t noncompressed_number_of_files = 1;
const size_t expire_log_minimum_files = 2;
const size_t num_mimimum_binlogs = expire_log_minimum_files + 2;
const auto expire_log_duration = 45s;

// Copy pasted from pinloki, should move to maxbase
bool has_extension(const std::string& file_name, const std::string& ext)
{
    if (auto pos = file_name.find_last_of(".");
        pos != std::string::npos
        && file_name.substr(pos + 1, std::string::npos) == ext)
    {
        return true;
    }
    else
    {
        return false;
    }
}
}

class CompressTest : public TestCase
{
public:
    using TestCase::TestCase;

    void create_data()
    {
        test.tprintf("Create table and insert data");
        test.expect(master.query("CREATE TABLE test.t1(s1 varchar(100), s2 varchar(100)"
                                 ", b1 bigint, b2 bigint)"),
                    " CREATE failed: %s",
                    master.error());

        const ssize_t ROWS = 1'000'000;
        const ssize_t CHUNK = 20'000;

        for (ssize_t r = 0; r < ROWS; r += CHUNK)
        {
            std::ostringstream os;
            os << "insert into test.t1 values ";
            for (ssize_t c = 0; c < CHUNK; ++c)
            {
                if (c)
                {
                    os << ',';
                }
                os << "('Navigare necesse est, vivere non est necesse',"
                   << "'Unus pro omnibus, omnes pro uno',"
                   << r << ", " << c << ')';
            }

            test.expect(master.query(os.str()), "Insert failed: %s", master.error());
        }
    }

    void wait_for_compression_to_finnish()
    {
        std::cout << "wait_for_compression_to_finnish" << std::endl;

        size_t expected_num_compressed = m_num_binlogs - noncompressed_number_of_files;
        size_t expected_num_noncompressed = m_num_binlogs - expected_num_compressed;
        size_t num_compressed = 0;
        size_t num_noncompressed = 0;

        bool done = false;
        while (!done && m_sw.split() < expire_log_duration)
        {
            num_compressed = 0;
            num_noncompressed = 0;
            auto rows = maxscale.rows("SHOW BINARY LOGS");
            if (!test.expect(m_num_binlogs == rows.size(), "Binglogs deleted or moved unexpectedly"))
            {
                break;
            }

            for (const auto& row : rows)
            {
                std::string file_name = row[0];
                has_extension(file_name, "zst") ? ++num_compressed : ++num_noncompressed;
            }

            if (expected_num_compressed == num_compressed
                && expected_num_noncompressed == num_noncompressed)
            {
                done = true;
                break;
            }

            std::this_thread::sleep_for(1s);
        }

        if (!done)
        {
            test.add_failure("Excpected %lu compressed files got %lu, and %lu non-compressed got %lu",
                             expected_num_compressed, num_compressed,
                             expected_num_noncompressed, num_noncompressed);
        }
    }

    void wait_for_archiving_to_finnish()
    {
        std::cout << "wait_for_archiving_to_finnish" << std::endl;

        size_t expected_num_archived = m_num_binlogs - expire_log_minimum_files;
        size_t num_archived = 0;
        bool done = false;

        while (!done && m_sw.split() < expire_log_duration + 5s)
        {
            auto rows = maxscale.rows("SHOW BINARY LOGS");
            auto res = test.maxscale->ssh_output("ls -l /tmp/archive | grep 000 | wc -l");
            test.expect(res.rc == 0, "Listing /tmp/archive contents should work");
            num_archived = std::stoul(res.output);

            if (num_archived == expected_num_archived)
            {
                done = true;
                break;
            }

            std::this_thread::sleep_for(1s);
        }

        if (!done)
        {
            test.add_failure("Expected %lu files to be archived but %lu were",
                             expected_num_archived, num_archived);
        }
    }

    void run() override
    {
        slave.query("STOP SLAVE");      // Start again when new data has been compressed
        m_sw.restart();
        create_data();
        std::cout << "Data created " << mxb::to_string(m_sw.split())
                  << " stopwatch restart" << std::endl;

        // Restart here, to compare against expire_log_duration
        m_sw.restart();
        sync(master, maxscale);

        m_num_binlogs = maxscale.rows("SHOW BINARY LOGS").size();
        if (num_mimimum_binlogs > m_num_binlogs)
        {
            test.add_failure("Only %lu binlogs were created. The test requires at least %lu binlogs.",
                             m_num_binlogs, num_mimimum_binlogs);
        }
        std::cout << "Pinloki synched " << mxb::to_string(m_sw.split())
                  << " there are " << m_num_binlogs << " binary logs " << std::endl;

        wait_for_compression_to_finnish();
        std::cout << "Compression finished " << mxb::to_string(m_sw.split()) << std::endl;

        // Start the slave, which will now cause pinloki to read compressed files
        // to serve the slave.
        slave.query("START SLAVE");
        // Give time to the slave to connect before checking that all binlogs
        // are still present.
        std::this_thread::sleep_for(250ms);

        size_t num_binlogs = maxscale.rows("SHOW BINARY LOGS").size();
        if (m_num_binlogs != num_binlogs || m_sw.split() >= expire_log_duration)
        {
            test.add_failure("Database setup and replication may be too slow. Increase"
                             " expire_log_duration in BOTH pinloki_compress_and_archive.cc"
                             " and pinloki_compress_and_archive.cnf");
        }
        else
        {
            std::cout << "Waiting for slave to sync with pinloki" << std::endl;
            sync(master, slave);
            std::cout << "Slave synched " << mxb::to_string(m_sw.split()) << std::endl;
            wait_for_archiving_to_finnish();
            std::cout << "Archiving done " << mxb::to_string(m_sw.split()) << std::endl;
        }
    }
private:
    mxb::StopWatch m_sw;
    size_t         m_num_binlogs;
};

int main(int argc, char** argv)
{
    TestConnections::skip_maxscale_start(true);
    TestConnections test(argc, argv);
    auto* master_srv = test.repl->backend(0);
    master_srv->stash_server_settings();
    master_srv->stop_database();
    master_srv->add_server_setting("max_binlog_size = 25M", "mysqld");
    master_srv->start_database();

    test.maxscale->ssh_node_f(true, "mkdir /tmp/archive 2&> /dev/null");
    test.maxscale->ssh_node_f(true, "chmod ao+rw /tmp/archive");
    test.maxscale->ssh_node_f(true, "rm -rf /tmp/archive/*");
    test.maxscale->start();

    auto res = CompressTest(test).result();

    master_srv->stop_database();
    master_srv->restore_server_settings();
    master_srv->start_database();

    return res;
}
