#pragma once
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

#include <maxbase/ccdefs.hh>

#include <string>
#include <mutex>
#include <memory>

#include <unistd.h>

namespace maxbase
{

// Minimal logger interface
class Logger
{
public:
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    virtual ~Logger()
    {
    }

    /**
     * Write a message to the log
     *
     * @param msg Message to write
     * @param len Length of message
     *
     * @return True on success
     */
    virtual bool write(const char* msg, int len) = 0;

    /**
     * Rotate the logfile
     *
     * @return True if the log was rotated
     */
    virtual bool rotate() = 0;

    /**
     * Get the name of the log file
     *
     * @return The name of the log file
     */
    const char* filename() const
    {
        return m_filename.c_str();
    }

protected:
    Logger(const std::string& filename):
        m_filename(filename)
    {
    }

    std::string m_filename;
};

class FileLogger: public Logger
{
public:
    FileLogger(const FileLogger&) = delete;
    FileLogger& operator=(const FileLogger&) = delete;

    /**
     * Create a new logger that writes to a file
     *
     * @param logdir Log file to open
     *
     * @return New logger instance or an empty unique_ptr on error
     */
    static std::unique_ptr<Logger> create(const std::string& filename);

    /**
     * Close the log
     *
     * A footer is written to the log and the file is closed.
     */
    ~FileLogger();

    /**
     * Write a message to the log
     *
     * @param msg Message to write
     * @param len Length of message
     *
     * @return True on success
     */
    bool write(const char* msg, int len);

    /**
     * Rotate the logfile by reopening it
     *
     * @return True if the log was rotated. False if the opening of the new file
     *         descriptor failed in which case the old file descriptor will be used.
     */
    bool rotate();

private:
    int              m_fd;
    std::mutex       m_lock;

    FileLogger(int fd, const std::string& filename);
    bool write_header();
    bool write_footer(const char* suffix);
    void close(const char* msg);
};

class StdoutLogger: public Logger
{
public:
    StdoutLogger(const StdoutLogger&) = delete;
    StdoutLogger& operator=(const StdoutLogger&) = delete;

    /**
     * Create a new logger that writes to stdout
     *
     * @param logdir Log file to open, has no functional effect on this logger
     *
     * @return New logger instance or an empty unique_ptr on error
     */
    static std::unique_ptr<Logger> create(const std::string& filename)
    {
        return std::unique_ptr<Logger>(new StdoutLogger(filename));
    }

    /**
     * Write a message to stdout
     *
     * @param msg Message to write
     * @param len Length of message
     *
     * @return True on success
     */
    bool write(const char* msg, int len)
    {
        return ::write(STDOUT_FILENO, msg, len) != -1;
    }

    /**
     * Rotate the "logfile"
     *
     * @return Always true
     */
    bool rotate()
    {
        return true;
    };

private:
    StdoutLogger(const std::string& filename):
        Logger(filename)
    {
    }
};

}
