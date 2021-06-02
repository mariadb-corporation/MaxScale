#include <maxtest/log.hh>

#include <cstdarg>
#include <future>
#include <sys/time.h>
#include <maxbase/format.hh>
#include <maxbase/string.hh>

using std::string;
namespace
{
const int sec_to_us = std::micro::den;
}

namespace maxtest
{
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
    fflush(stdout);
    string full_msg;
    full_msg.reserve(timeinfo.length() + 2 + msg.length());
    full_msg.append(timeinfo).append(": ").append(msg);
    std::lock_guard<std::mutex> guard(m_lock);
    m_fails.push_back(move(full_msg));
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
    string rval;
    std::lock_guard<std::mutex> guard(m_lock);
    rval = mxb::create_list_string(m_fails, "\n");
    return rval;
}

void TestLogger::log_msgf(const char* format, ...)
{
    va_list valist;
    va_start(valist, format);
    log_msg(format, valist);
    va_end(valist);
}

void TestLogger::log_msg(const char* format, va_list args)
{
    string msg = prepare_msg(format, args);
    string timeinfo = time_string();
    printf("%s: %s\n", timeinfo.c_str(), msg.c_str());
}

void TestLogger::log_msg(const string& str)
{
    log_msgf("%s", str.c_str());
}

std::string TestLogger::time_string() const
{
    timeval now {0};
    gettimeofday(&now, nullptr);

    // Generate time stamp.
    struct tm broken_down_time { 0 };
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

int TestLogger::time_elapsed_s() const
{
    auto now_s = time(nullptr);
    auto start_s = m_start_time_us / sec_to_us;
    return now_s - start_s;
}

bool SharedData::concurrent_run(const BoolFuncArray& funcs)
{
    bool rval = true;
    if (settings.allow_concurrent_run)
    {
        std::vector<std::future<bool>> futures;
        futures.reserve(funcs.size());

        for (auto& func : funcs)
        {
            futures.emplace_back(std::async(std::launch::async, func));
        }

        for (auto& fut : futures)
        {
            if (!fut.get())
            {
                rval = false;
            }
        }
    }
    else
    {
        for (auto& func : funcs)
        {
            if (!func())
            {
                rval = false;
            }
        }
    }
    return rval;
}

bool SharedData::run_shell_command(const string& cmd, const string& errmsg)
{
    bool rval = true;
    auto cmdc = cmd.c_str();

    int rc = system(cmdc);
    if (rc != 0)
    {
        rval = false;
        string msgp2 = mxb::string_printf("Shell command '%s' returned %i.", cmdc, rc);
        if (errmsg.empty())
        {
            log.add_failure("%s", msgp2.c_str());
        }
        else
        {
            log.add_failure("%s %s", errmsg.c_str(), msgp2.c_str());
        }
    }
    return rval;
}

std::string cutoff_string(const string& source, char cutoff)
{
    auto pos = source.find(cutoff);
    return (pos != string::npos) ? source.substr(0, pos) : source;
}
}
