/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-05-22
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/ccdefs.hh>
#include <functional>
#include <memory>
#include <unistd.h>

class ExternalCmd
{
public:
    using OutputHandler = std::function<void (const std::string&, const std::string&)>;

    static constexpr int ERROR = -1;    // System error that's unrelated to the command being executed
    static constexpr int TIMEOUT = -2;  // Command hasn't exited yet

    ~ExternalCmd();

    /**
     * Create a new external command. The name and parameters are copied so
     * the original memory can be freed.
     *
     * @param argstr  Command to execute with the parameters
     * @param timeout Command timeout in seconds
     * @param handler Output handler to use. By default the output is logged into the MaxScale log.
     *
     * @return Pointer to new external command struct or NULL if an error occurred
     */
    static std::unique_ptr<ExternalCmd> create(const std::string& argstr, int timeout,
                                               OutputHandler handler = {});

    /**
     * Run the command
     *
     * Starts the command and waits for it to complete. Any output is redirected into the output
     * hander. This is the same as running start() and then calling wait().
     *
     * @return The return value of the executed command or -1 on error
     */
    int run();

    /**
     * Start the command and return immediately.
     *
     * @return True if the command was started successfully.
     */
    bool start();

    /**
     * Write data into the command's stdin
     *
     * The timeout defined during command creation is also used as the timeout for writes.
     *
     * @param ptr Pointer to data to be written
     * @param len Length of the data
     *
     * @return True if the writing was successful
     */
    bool write(const void* ptr, int64_t len);

    /**
     * Close the write end of the pipe that's connected to the command
     *
     * This signals the command that no more data is readable and that it should exit.
     */
    void close_output();

    /**
     * Try to wait for the process.
     *
     * @return The process return code if it had already stopped, ERROR if the waiting failed or TIMEOUT if
     *         the process had not yet exited. Once the function returns something other than TIMEOUT, all
     *         calls to try_wait() or wait() will return the result of the operation.
     */
    int try_wait();

    /**
     * Wait for the process to exit.
     *
     * @return The process return code if the process had stopped or ERROR if the waiting failed.
     */
    int wait();

    /**
     * If keyword is found in command script, replace keyword with output of generator function.
     *
     * @param keyword Keyword to replace
     * @param generator Function which generates the replacement string. Only ran if keyword was found.
     */
    void match_substitute(const std::string& keyword, const std::function<std::string(void)>& generator);

    /**
     * Reset substituted command to the unaltered command. Should be ran before a substitution pass begins.
     */
    void reset_substituted();

    const char* substituted() const;

private:
    static const int MAX_ARGS {256};

    std::string   m_orig_command;       /**< Original command */
    std::string   m_subst_command;      /**< Command with substitutions */
    std::string   m_cmd;
    std::string   m_output;
    int           m_timeout;            /**< Command timeout in seconds */
    int           m_pid {-1};
    int           m_result {TIMEOUT};
    int           m_read_fd{-1};
    int           m_write_fd{-1};
    OutputHandler m_handler;

    ExternalCmd(const std::string& script, int timeout, OutputHandler handler);

    int tokenize_args(char* dest[], int dest_size);

    void read_output();

    /**
     * Substitute all occurrences of @c match with @c replace in the arguments.
     *
     * @param match Match string
     * @param replace Replacement string
     */
    void substitute_arg(const std::string& match, const std::string& replace);
};
