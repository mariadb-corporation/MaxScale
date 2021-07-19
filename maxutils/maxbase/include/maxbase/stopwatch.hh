/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-07-19
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxbase/ccdefs.hh>
#include <chrono>
#include <iosfwd>
#include <string>
#include <sys/time.h>

namespace maxbase
{

/**
 *  The MaxScale "standard" steady clock. Do not use this directly,
 *  use Clock declared further down (specifically, use Clock::now()).
 */
using SteadyClock = std::chrono::steady_clock;
using Duration = SteadyClock::duration;
using TimePoint = SteadyClock::time_point;

inline Duration from_secs(double secs)
{
    return Duration {Duration::rep(secs * Duration::period::den / Duration::period::num)};
}

inline double to_secs(Duration dur)
{
    return std::chrono::duration<double>(dur).count();
}

/**
 * @brief  timespec_to_duration
 * @param  ts       - a timespec
 * @return Duration - timespec has nanosecond precision. The return value is cast to whatever
 *                    precision the Duration template parameter has.
 */
template<typename Duration>
inline Duration timespec_to_duration(timespec ts)
{
    auto nanos {std::chrono::seconds {ts.tv_sec} + std::chrono::nanoseconds {ts.tv_nsec}};
    return std::chrono::duration_cast<Duration>(nanos);
}

/**
 * @brief  timespec_to_time_point
 * @param  timespec ts -  A timespec suitable for the Clock template parameter.
 *                        For the system clock, it should be a duration since the last unix epoch.
 * @return Clock::time_point
 */
template<typename Clock>
inline typename Clock::time_point timespec_to_time_point(timespec ts)
{
    return typename Clock::time_point {maxbase::timespec_to_duration<typename Clock::duration>(ts)};
}

/**
 *  @brief NowType enum
 *
 *  EPollTick - Use worker::epoll_tick_now(), no performance penalty.
 *              The thread must be a worker thread (can relax later).
 *  RealTime  - Use real-time, but remember this goes to the kernel.
 *              The thread does not need to be a Worker thread.
 */
enum class NowType {EPollTick, RealTime};

/**
 *   @class Clock
 *
 *   MaxScale "standard" clock. It is exactly the same as std::chrono::steady_clock
 *   except it redefines the static member function now().
 */
struct Clock : public SteadyClock
{
    static TimePoint now(NowType type = NowType::RealTime) noexcept;
};

/**
 *  @class StopWatch
 *
 *  Simple stopwatch for measuring time.
 *
 *  Example usage:
 *    auto limit = maxbase::Duration(std::chrono::milliseconds(100));
 *
 *    maxbase::StopWatch sw;
 *    foo();
 *    auto duration = sw.split();
 *
 *    std::cout << "foo duration " << duration << std::endl;
 *    if (duration > limit)
 *    {
 *        maxbase::Duration diff = duration - limit; // no auto, would become Clock::duration.
 *        std::cerr << "foo exceeded the limit " << limit << " by "  << diff << std::endl;
 *    }
 *  Possible output:
 *    foo duration 100.734ms
 *    foo exceeded the limit 100ms by 733.636us
 */
class StopWatch
{
public:
    /** Create and start the stopwatch. */
    StopWatch();

    /** Split time. Overall duration since creation or last restart(). */
    Duration split() const;

    /** Lap time. Time since last lap() call, or if lap() was not called, creation or last restart(). */
    Duration lap();

    /** Return split time and restart stopwatch. */
    Duration restart();
private:
    TimePoint m_start;
    TimePoint m_lap;
};

/**
 *  @class Timer
 *
 * This class is primarily meant for doing something periodically, e.g. output something every 5 seconds.
 */
class Timer
{
public:
    /** Tick_duration determines the timer frequency. To reset the Timer, or change the tick, just
     *  assign my_timer = Timer(std::chrono::seconds(5)).
     */
    Timer(Duration tick_duration);

    /** Returns the number of ticks since the last alarm point. If called continuously, a Timer will
     *  return '1' at tick_duration rate. If the Timer is not called for some time, it returns the
     *  number of ticks since the last alarm point, i.e. it returns 1 + number_of_missed_ticks.
     */
    int64_t alarm() const;

    /** Same as alarm(), but sleeps until the next alarm if it has not already happened.
     */
    int64_t wait_alarm() const;

    /**
     * @brief until_alarm
     *
     * @return Duration until next alarm, or Duration::zero() if the alarm is due.
     */
    Duration until_alarm() const;

    /** The duration of tick(s). Calling my_timer.tick_duration(my_timer.alarm()) can be handy when
     *  a duration, rather than ticks is needed.
     */
    Duration tick_duration(int64_t ticks = 1) const
    {
        return m_dur * ticks;
    }
private:
    const Duration  m_dur;
    const TimePoint m_start = Clock::now();
    mutable int64_t m_last_alarm_ticks = 0;
};

/** IntervalTimer for accumulating intervals (i.e. durations). Do not expect many very short
 *  durations to accumulate properly (unless you have a superfast processor, RTLinux, etc.)
 *
 * Usage pattern:
 * IntervalTimer timer;  // created ahead of time.
 * ...
 * In some sort of a loop (explicit or implicit):
 * timer.start_interval();
 * foo();
 * timer.end_interval();
 * ...
 * And finally:
 * std::cout << timer.total() << std::endl;
 *
 */
class IntervalTimer
{
public:
    /** Create but do not start the intervaltimer, i.e. starting in paused mode. */
    IntervalTimer();

    /** Resume measuring time. Ok to call multiple times without an end_interval(). */
    void start_interval();

    /** Pause measuring time. Ok to call without a start_interval. */
    void end_interval();

    /** Total duration of intervals (thus far). */
    Duration total() const;
private:
    TimePoint m_last_start;
    Duration  m_total;
};

/** Returns the duration as a double and string adjusted to a suffix like ms for milliseconds.
 *  The double and suffix (unit) combination is selected to be easy to read.
 *  This is for output conveniece. You can always convert a duration to a specific unit:
 *  long ms {std::chrono::duration_cast<std::chrono::milliseconds>(dur).count()};
 */
std::pair<double, std::string> dur_to_human_readable(Duration dur);

/** Create a string using dur_to_human_readable, std::ostringstream << d.first << sep << d.second. */
std::string to_string(Duration dur, const std::string& sep = "");

/** Stream to os << d.first << d.second. Not using to_string(), which would use a default stream. */
std::ostream& operator<<(std::ostream& os, Duration dur);

/** TimePoint to string, formatted using strftime formats. */
std::string to_string(TimePoint tp, const std::string& fmt = "%F %T");

/** Stream to std::ostream using to_string(tp) */
std::ostream& operator<<(std::ostream& os, TimePoint tp);
}

namespace wall_time
{

using Clock = std::chrono::system_clock;
using Duration = Clock::duration;
using TimePoint = Clock::time_point;

/** system_clock::timepoint to string, formatted using strftime formats */
std::string to_string(TimePoint tp, const std::string& fmt = "%F %T");

/** Stream to std::ostream using to_string(tp) */
std::ostream& operator<<(std::ostream& os, TimePoint tp);
}
