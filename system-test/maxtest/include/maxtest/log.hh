/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#pragma once

#include <maxtest/ccdefs.hh>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace maxtest
{

struct CmdResult
{
    int         rc{-1};
    std::string output;
};

using BoolFuncArray = std::vector<std::function<bool (void)>>;

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
    bool local_maxscale {false};        /**< MaxScale running locally */
    bool allow_concurrent_run {true};   /**< Allow concurrent_run to run concurrently */
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

    /**
     * Run a shell command locally, reading output. Failure is not a test error.
     *
     * @param cmd Command string
     * @return Return value and output
     */
    mxt::CmdResult run_shell_cmd_output(const std::string& cmd);
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
