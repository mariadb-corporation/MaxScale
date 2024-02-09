/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-01-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxbase/ccdefs.hh>
#include <functional>
#include <memory>
#include <optional>
#include <unistd.h>

namespace maxbase
{
class Process
{
public:
    static constexpr int ERROR = -1;    // System error that's unrelated to the command being executed
    static constexpr int TIMEOUT = -2;  // Command hasn't exited yet

    enum class RedirStdErr {YES, NO};

    ~Process();

    /**
     * Try to wait for the process.
     *
     * @return The process return code if it had already stopped, ERROR if the waiting failed or TIMEOUT if
     *         the process had not yet exited. Once the function returns something other than TIMEOUT, all
     *         calls to try_wait() or wait() will return the result of the operation.
     */
    virtual int try_wait();

    /**
     * Wait for the process to exit.
     *
     * @return The process return code if the process had stopped or ERROR if the waiting failed.
     */
    int wait();

    /**
     * Close the write end of the pipe that's connected to the command
     *
     * This signals the command that no more data is readable and that it should exit.
     */
    void close_output();

    struct Info
    {
        pid_t       pid {-1};
        int         read_fd {-1};
        int         write_fd {-1};
        std::string exec_name;
    };

    enum class ForkType {FORK, SPAWN};
    /**
     * Start external process and return process information.
     *
     * @param cmd Command string
     * @param redirect Redirect stderror of subprocess
     * @param fork_type Subprocess launch mode
     * @return Process information
     */
    static std::optional<Info> start_external_cmd(const std::string& cmd, RedirStdErr redirect,
                                                  ForkType fork_type);

protected:
    Process(Info info, int timeout_ms);

    /**
     * Start external process and save process info to current object.
     *
     * @param cmd Command string
     * @param redirect Redirect stderror of subprocess
     * @return True if process was launched
     */
    bool start_set_external_cmd(const std::string& cmd, RedirStdErr redirect);

    const Info& proc_info() const;
    int         timeout_ms() const;

private:
    static std::optional<Info> fork_external_cmd(const std::string& cmd, RedirStdErr redirect);
    static std::optional<Info> spawn_external_cmd(const std::string& cmd, RedirStdErr redirect);

    Info m_proc_info;
    int  m_timeout_ms {-1};
    int  m_result {TIMEOUT};
};

class ExternalCmd final : public Process
{
public:
    using OutputHandler = std::function<void (const std::string&, const std::string&)>;

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
                                               OutputHandler handler);

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
     * Runs base class version + reads output.
     */
    int try_wait() override;

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
    std::string   m_orig_command;       /**< Original command */
    std::string   m_subst_command;      /**< Command with substitutions */
    std::string   m_output;
    OutputHandler m_handler;

    ExternalCmd(const std::string& script, int timeout, OutputHandler handler);

    void read_output();

    /**
     * Substitute all occurrences of @c match with @c replace in the arguments.
     *
     * @param match Match string
     * @param replace Replacement string
     */
    void substitute_arg(const std::string& match, const std::string& replace);
};

class AsyncProcess final : public Process
{
public:
    AsyncProcess(Info info, int timeout_ms);

    /**
     * Read external process output.
     *
     * @return A valid value on success. Can return an empty string if no data was read.
     */
    std::optional<std::string> read_output();

    int read_fd() const;

    /**
     * Write data into the command's stdin.
     *
     * @param ptr Pointer to data to be written
     * @param len Length of the data
     *
     * @return True if the writing was successful. The write must succeed without blocking, so only
     * small amounts of data should be written.
     */
    bool write(const uint8_t* ptr, size_t len);

    int try_wait() override;

private:
    std::string m_output;
};

class AsyncCmd
{
public:
    static std::unique_ptr<AsyncCmd> create(const std::string& cmd, int timeout_ms);
    std::unique_ptr<AsyncProcess>    start();

private:
    AsyncCmd(const std::string& cmd, int timeout_ms);

    std::string m_cmd;
    int         m_timeout_ms {-1};
};
}
