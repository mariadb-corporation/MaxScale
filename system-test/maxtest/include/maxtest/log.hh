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

#pragma once

#include <maxtest/ccdefs.hh>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>
#include <maxbase/ini.hh>

namespace maxtest
{

struct CmdResult
{
    int         rc{-1};
    std::string output;
};

using BoolFuncArray = std::vector<std::function<bool (void)>>;
using NetworkConfig = std::map<std::string, std::string>;

/**
 * System test error log container.
 */
class TestLogger
{
public:
    TestLogger();

    void        expect(bool result, const char* format, ...) __attribute__ ((format(printf, 3, 4)));
    void        add_failure(const char* format, ...) __attribute__ ((format(printf, 2, 3)));
    void        add_failure_v(const char* format, va_list args);
    void        expect_v(bool result, const char* format, va_list args);
    std::string all_errors_to_string();
    std::string latest_error();

    void log_msgf(const char* format, ...) __attribute__ ((format(printf, 2, 3)));
    void log_msg(const char* format, va_list args);
    void log_msg(const std::string& str);
    void reset_timer();
    int  time_elapsed_s() const;

    int m_n_fails {0};      /**< Number of test fails. TODO: private */

private:
    int64_t                  m_start_time_us {0};
    std::vector<std::string> m_fails;
    std::mutex               m_lock;    /**< Protects against concurrent logging */

    std::string time_string() const;
    std::string prepare_msg(const char* format, va_list args) const;
};

/**
 * Various global settings.
 */
struct Settings
{
    bool verbose {false};               /**< True if printing more details */
    bool allow_concurrent_run {true};   /**< Allow concurrent_run to run concurrently */

    /**< True when running test with mdbci. Mdbci allows VM creation during test start. If false, backend
     * info is read from config file and any missing backends is an error. */
    bool mdbci_test {true};
};

/**
 * Data shared across test classes.
 */
struct SharedData
{
    TestLogger  log;        /**< Error log container */
    Settings    settings;
    std::string test_name;      /**< Test name */

    bool concurrent_run(const BoolFuncArray& funcs);

    /**
     * Run a shell command locally. Failure is a test error.
     *
     * @param cmd Command string
     * @param errmsg Optional error message
     * @return True on success
     */
    bool run_shell_command(const std::string& cmd, const std::string& errmsg);

    bool run_shell_cmdf(const char* fmt, ...) mxb_attribute((format (printf, 2, 3)));

    /**
     * Run a shell command locally, reading output. Failure is not a test error.
     *
     * @param cmd Command string
     * @return Return value and output
     */
    mxt::CmdResult run_shell_cmd_output(const std::string& cmd);

    std::string get_nc_item(const NetworkConfig& nwconfig, const std::string& search_key);

    bool read_str(const mxb::ini::map_result::ConfigSection& cnf, const std::string& key, std::string& dest);
    bool read_int(const mxb::ini::map_result::ConfigSection& cnf, const std::string& key, int& dest);
};

/**
 * Return substring before first cutoff char.
 *
 * @param source Source string
 * @param cutoff Cutoff character
 * @return Result
 */
std::string cutoff_string(const std::string& source, char cutoff);
}
