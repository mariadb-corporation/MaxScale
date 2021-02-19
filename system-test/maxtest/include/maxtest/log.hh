#pragma once

#include <string>
#include <vector>

/**
 * System test error log container.
 */
class TestLogger
{
public:
    TestLogger();

    void expect(bool result, const char* format, ...) __attribute__ ((format(printf, 3, 4)));
    void add_failure(const char* format, ...) __attribute__ ((format(printf, 2, 3)));
    void add_failure_v(const char* format, va_list args);
    void expect_v(bool result, const char* format, va_list args);
    std::string all_errors_to_string();

    void log_msg(const char* format, va_list args);
    void reset_timer();

    int m_n_fails {0}; /**< Number of test fails. TODO: private */

private:
    int64_t                   m_start_time_us {0};
    std::vector<std::string>  m_fails;

    std::string time_string() const;
    std::string prepare_msg(const char* format, va_list args) const;
};

struct SharedData
{
    TestLogger log;             /**< Error log container */
    bool       verbose {false}; /**< True if printing more details */
};
