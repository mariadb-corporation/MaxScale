/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2022-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxbase/logger.hh>

#include <syslog.h>
#include <fcntl.h>
#include <unistd.h>

#include <chrono>
#include <cstring>
#include <cstdio>
#include <ctime>

#include <maxbase/error.h>

// TODO: move <maxscale/debug.h> into maxbase
#define ss_dassert(a)

/**
 * Error logging for the logger itself.
 *
 * For obvious reasons, it cannot use its own functions for reporting errors.
 */
#define LOG_ERROR(format, ...) do { fprintf(stderr, format, ##__VA_ARGS__); } while (false)

//
// Helper functions
//
namespace
{

int open_fd(const std::string& filename)
{
    int fd = open(filename.c_str(), O_WRONLY | O_APPEND | O_CREAT,
                  S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH);

    if (fd == -1)
    {
        LOG_ERROR("Failed to open file '%s': %d, %s\n", filename.c_str(), errno, mxb_strerror(errno));
    }

    return fd;
}

bool should_log_error()
{
    using std::chrono::seconds;
    static auto last_write = std::chrono::steady_clock::now() - seconds(61);
    auto now = std::chrono::steady_clock::now();
    bool rval = false;

    if ((now - last_write).count() >= 60)
    {
        last_write = now;
        rval = true;
    }

    return rval;
}

}

namespace maxbase
{

//
// Public methods
//

std::unique_ptr<Logger> FileLogger::create(const std::string& filename)
{
    std::unique_ptr<FileLogger> logger;
    int fd = open_fd(filename);

    if (fd != -1)
    {
        logger.reset(new (std::nothrow) FileLogger(fd, filename));

        if (logger)
        {
            logger->write_header();
        }
        else
        {
            ::close(fd);
        }
    }

    return std::move(logger);
}

FileLogger::~FileLogger()
{
    std::lock_guard<std::mutex> guard(m_lock);
    ss_dassert(m_fd != -1);
    close("MariaDB MaxScale is shut down.");
}

bool FileLogger::write(const char* msg, int len)
{
    bool rval = true;
    std::lock_guard<std::mutex> guard(m_lock);

    while (len > 0)
    {
        int rc;
        do
        {
            rc = ::write(m_fd, msg, len);
        }
        while (rc == -1 && errno == EINTR);

        if (rc == -1)
        {
            if (should_log_error()) // Coarse error suppression
            {
                LOG_ERROR("Failed to write to log: %d, %s\n", errno, mxb_strerror(errno));
            }

            rval = false;
            break;
        }

        // If write only writes a part of the message, retry again
        len -= rc;
        msg += rc;
    }

    return rval;
}

bool FileLogger::rotate()
{
    std::lock_guard<std::mutex> guard(m_lock);
    int fd = open_fd(m_filename);

    if (fd != -1)
    {
        close("File closed due to log rotation.");
        m_fd = fd;
    }

    return fd != -1;
}

//
// Private methods
//

FileLogger::FileLogger(int fd, const std::string& filename):
    Logger(filename),
    m_fd(fd)
{
}

void FileLogger::close(const char* msg)
{
    write_footer(msg);
    ::close(m_fd);
    m_fd = -1;
}

// Nearly identical to the one in log_manager.cc
bool FileLogger::write_header()
{
    time_t t = time(NULL);
    struct tm tm;
    localtime_r(&t, &tm);

    const char PREFIX[] = "MariaDB MaxScale  "; // sizeof(PREFIX) includes the NULL.
    char time_string[32]; // 26 would be enough, according to "man asctime".
    asctime_r(&tm, time_string);

    size_t size = sizeof(PREFIX) + m_filename.length() + 2 * sizeof(' ') + strlen(time_string);

    char header[size + 2]; // For the 2 newlines.
    sprintf(header, "\n\n%s%s  %s", PREFIX, m_filename.c_str(), time_string);

    char line[sizeof(header) - 1];
    memset(line, '-', sizeof(line) - 1);
    line[sizeof(line) - 1] = '\n';

    bool ok = ::write(m_fd, header, sizeof(header) - 1) != -1 &&
              ::write(m_fd, line, sizeof(line)) != -1;

    if (!ok)
    {
        LOG_ERROR("Error: Writing log header failed due to %d, %s\n",
                  errno, mxb_strerror(errno));
    }

    return ok;
}

// Nearly identical to the one in log_manager.cc
bool FileLogger::write_footer(const char* suffix)
{
    time_t t = time(NULL);
    struct tm tm;
    localtime_r(&t, &tm);

    const char FORMAT[] = "%04d-%02d-%02d %02d:%02d:%02d";
    char time_string[20]; // 19 chars + NULL.

    sprintf(time_string, FORMAT,
            tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);

    size_t size = sizeof(time_string) + 3 * sizeof(' ') + strlen(suffix) + sizeof('\n');

    char header[size];
    sprintf(header, "%s   %s\n", time_string, suffix);

    char line[sizeof(header) - 1];
    memset(line, '-', sizeof(line) - 1);
    line[sizeof(line) - 1] = '\n';

    bool ok = ::write(m_fd, header, sizeof(header) - 1) != -1 &&
              ::write(m_fd, line, sizeof(line)) != -1;

    if (!ok)
    {
        LOG_ERROR("Error: Writing log footer failed due to %d, %s\n",
                  errno, mxb_strerror(errno));
    }

    return ok;
}

}
