#include <maxtest/log.hh>

#include <cstdarg>
#include <ratio>
#include <sys/time.h>
#include <maxbase/format.hh>
#include <maxbase/string.hh>

using std::string;
namespace
{
const int sec_to_us = std::micro::den;
}

void TestLogger::add_failure(const char* format, ...)
{
    va_list valist;
    va_start(valist, format);
    add_failure_v(format, valist);
    va_end(valist);
}

void TestLogger::add_failure_v(const char* format, va_list args)
{
    string msg = prepare_msg(format, args);
    string timeinfo = time_string();

    printf("%s: TEST_FAILED! %s\n", timeinfo.c_str(), msg.c_str());
    m_fails.push_back(timeinfo + ": " + msg);
    m_n_fails++;
}

void TestLogger::expect(bool result, const char* format, ...)
{
    va_list valist;
    va_start(valist, format);
    expect_v(result, format, valist);
    va_end(valist);
}

void TestLogger::expect_v(bool result, const char* format, va_list args)
{
    if (!result)
    {
        add_failure_v(format, args);
    }
}

TestLogger::TestLogger()
{
    reset_timer();
}

std::string TestLogger::all_errors_to_string()
{
    return mxb::create_list_string(m_fails, "\n");
}

void TestLogger::log_msg(const char* format, va_list args)
{
    string msg = prepare_msg(format, args);
    string timeinfo = time_string();
    printf("%s: %s\n", timeinfo.c_str(), msg.c_str());
}

std::string TestLogger::time_string() const
{
    timeval now {0};
    gettimeofday(&now, nullptr);

    // Generate time stamp.
    struct tm broken_down_time {0};
    localtime_r(&now.tv_sec, &broken_down_time);
    char timebuf[10];
    strftime(timebuf, sizeof(timebuf), "%T", &broken_down_time);

    int64_t elapsed_time_us = (sec_to_us * now.tv_sec + now.tv_usec) - m_start_time_us;
    double elapsed_time_s = (double)elapsed_time_us / sec_to_us;

    return mxb::string_printf("%s %5.1fs", timebuf, elapsed_time_s);
}

std::string TestLogger::prepare_msg(const char* format, va_list args) const
{
    string msg = mxb::string_vprintf(format, args);
    if (!msg.empty() && msg.back() == '\n')
    {
        msg.pop_back();
    }
    return msg;
}

void TestLogger::reset_timer()
{
    timeval now {0};
    gettimeofday(&now, nullptr);
    m_start_time_us = sec_to_us * now.tv_sec + now.tv_usec;
}
