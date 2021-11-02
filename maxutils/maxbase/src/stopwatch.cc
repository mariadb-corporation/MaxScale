/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-10-29
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxbase/stopwatch.hh>
#include <maxbase/worker.hh>

#include <iomanip>
#include <iostream>
#include <sstream>
#include <ctime>
#include <thread>

namespace maxbase
{

TimePoint Clock::now(NowType type) noexcept
{
    if (type == NowType::RealTime)
    {
        return std::chrono::steady_clock::now();
    }
    else
    {
        mxb_assert(maxbase::Worker::get_current());
        mxb_assert(type == NowType::EPollTick);
        return maxbase::Worker::get_current()->epoll_tick_now();
    }
}

StopWatch::StopWatch()
{
    restart();
}

Duration StopWatch::split() const
{
    return {Clock::now() - m_start};
}

Duration StopWatch::lap()
{
    auto now = Clock::now();
    Duration lap = now - m_lap;
    m_lap = now;
    return lap;
}

Duration StopWatch::restart()
{
    TimePoint now = Clock::now();
    Duration split = now - m_start;
    m_start = m_lap = now;
    return split;
}

Timer::Timer(Duration tick_duration)
    : m_dur(tick_duration)
{
}

int64_t Timer::alarm() const
{
    auto total_ticks = (Clock::now() - m_start) / m_dur;
    int64_t ticks = total_ticks - m_last_alarm_ticks;
    m_last_alarm_ticks += ticks;

    return ticks;
}

int64_t Timer::wait_alarm() const
{
    auto now = Clock::now();
    auto total_ticks = (now - m_start) / m_dur;
    int64_t ticks = total_ticks - m_last_alarm_ticks;

    if (!ticks)
    {
        Duration d = (total_ticks + 1) * m_dur - (now - m_start);
        std::this_thread::sleep_for(d);
    }

    // This while loop is for the case when sleep_for() returns too early (clock resolution, rouding error).
    // Hypothetical, could not get this to trigger in testing.
    while ((ticks = alarm()) == 0)
    {
        std::this_thread::sleep_for(std::chrono::microseconds(1));
    }

    return ticks;
}

Duration Timer::until_alarm() const
{
    auto now = Clock::now();
    auto total_ticks = (now - m_start) / m_dur;
    int64_t ticks = total_ticks - m_last_alarm_ticks;

    Duration ret;

    if (ticks)
    {
        ret = Duration::zero();
    }
    else
    {
        ret = (total_ticks + 1) * m_dur - (now - m_start);
    }

    return ret;
}


IntervalTimer::IntervalTimer()
    : m_total(0)
{
}

void IntervalTimer::start_interval()
{
    m_last_start = Clock::now();
}

void IntervalTimer::end_interval()
{
    if (m_last_start == maxbase::TimePoint())
    {
        // m_last_start is defaulted. Ignore, avoids extra logic at call sites.
        return;
    }

    m_total += Clock::now() - m_last_start;
    // reset to make it easier to spot usage bugs, like calling end_interval(); end_interval();
    m_last_start = TimePoint();
}

Duration IntervalTimer::total() const
{
    return m_total;
}
}   // maxbase

/********** OUTPUT ***********/
namespace
{
using namespace maxbase;
struct TimeConvert
{
    double      div;        // divide the value of the previous unit by this
    std::string suffix;     // milliseconds, hours etc.
    double      max_visual; // threashold to switch to the next unit
};
// Will never get to centuries because the duration is a long carrying nanoseconds
TimeConvert convert[]
{
    {1, "ns", 1000}, {1000, "us", 1000}, {1000, "ms", 1000},
    {1000, "s", 60}, {60, "min", 60}, {60, "hours", 24},
    {24, "days", 365.25}, {365.25, "years", 10000},
    {100, "centuries", std::numeric_limits<double>::max()}
};

int convert_size = sizeof(convert) / sizeof(convert[0]);
}

namespace maxbase
{

std::pair<double, std::string> dur_to_human_readable(Duration dur)
{
    using namespace std::chrono;
    double time = duration_cast<nanoseconds>(dur).count();
    bool negative = (time < 0) ? time = -time, true : false;

    for (int i = 0; i <= convert_size; ++i)
    {
        if (i == convert_size)
        {
            return std::make_pair(negative ? -time : time,
                                  convert[convert_size - 1].suffix);
        }

        time /= convert[i].div;

        if (time < convert[i].max_visual)
        {
            return std::make_pair(negative ? -time : time, convert[i].suffix);
        }
    }

    abort();    // should never get here
}

std::string to_string(Duration dur, const std::string& sep)
{
    auto p = dur_to_human_readable(dur);
    std::ostringstream os;
    os << p.first << sep << p.second;

    return os.str();
}

std::ostream& operator<<(std::ostream& os, Duration dur)
{
    auto p = dur_to_human_readable(dur);
    os << p.first << p.second;

    return os;
}


std::string to_string(TimePoint tp, const std::string& fmt)
{
    auto in_wall_time = wall_time::Clock::now() + (tp - Clock::now());

    return wall_time::to_string(in_wall_time, fmt);
}

std::ostream& operator<<(std::ostream& os, TimePoint tp)
{
    os << to_string(tp);
    return os;
}

void test_stopwatch_output(std::ostream& os)
{
    long long dur[] =
    {
        400,                                // 400ns
        5 * 1000,                           // 5us
        500 * 1000,                         // 500us
        1 * 1000000,                        // 1ms
        700 * 1000000LL,                    // 700ms
        5 * 1000000000LL,                   // 5s
        200 * 1000000000LL,                 // 200s
        5 * 60 * 1000000000LL,              // 5m
        45 * 60 * 1000000000LL,             // 45m
        130 * 60 * 1000000000LL,            // 130m
        24 * 60 * 60 * 1000000000LL,        // 24 hours
        3 * 24 * 60 * 60 * 1000000000LL,    // 72 hours
        180 * 24 * 60 * 60 * 1000000000LL,  // 180 days
        1000 * 24 * 60 * 60 * 1000000000LL  // 1000 days
    };

    for (unsigned i = 0; i < sizeof(dur) / sizeof(dur[0]); ++i)
    {
        os << Duration(dur[i]) << std::endl;
    }
}
}

namespace wall_time
{
std::string to_string(TimePoint tp, const std::string& fmt)
{
    std::time_t timet = std::chrono::system_clock::to_time_t(tp);

    struct tm tm;
    localtime_r(&timet, &tm);
    const int sz = 1024;
    char buf[sz];
    strftime(buf, sz, fmt.c_str(), &tm);
    return buf;
}

std::ostream& operator<<(std::ostream& os, TimePoint tp)
{
    os << to_string(tp);
    return os;
}
}
