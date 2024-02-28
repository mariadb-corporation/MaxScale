/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-02-27
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxbase/externcmd.hh>

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <spawn.h>
#include <string.h>
#include <sys/wait.h>
#include <thread>
#include <utility>

#include <maxbase/assert.hh>
#include <maxbase/alloc.hh>
#include <maxbase/string.hh>
#include <maxbase/stopwatch.hh>

using std::string;

namespace
{
bool check_executable(const std::string& cmd);

int tokenize_args(const string& args_str, char* dest[], int dest_size)
{
    bool quoted = false;
    bool read = false;
    bool escaped = false;
    char qc = 0;

    char args[args_str.length() + 1];
    strcpy(args, args_str.c_str());
    char* start = args;
    char* ptr = start;
    int i = 0;

    while (*ptr != '\0' && i < dest_size)
    {
        if (escaped)
        {
            escaped = false;
        }
        else
        {
            if (*ptr == '\\')
            {
                escaped = true;
            }
            else if (quoted && *ptr == qc)      /** End of quoted string */
            {
                *ptr = '\0';
                dest[i++] = MXB_STRDUP(start);
                read = false;
                quoted = false;
            }
            else if (!quoted)
            {
                if (isspace(*ptr))
                {
                    *ptr = '\0';
                    if (read)   /** New token */
                    {
                        dest[i++] = MXB_STRDUP(start);
                        read = false;
                    }
                }
                else if (*ptr == '\"' || *ptr == '\'')
                {
                    /** New quoted token, strip quotes */
                    quoted = true;
                    qc = *ptr;
                    start = ptr + 1;
                }
                else if (!read)
                {
                    start = ptr;
                    read = true;
                }
            }
        }
        ptr++;
    }
    if (read)
    {
        dest[i++] = MXB_STRDUP(start);
    }
    return i;
}

bool check_executable(const string& cmd)
{
    bool success = false;
    char* argvec[1] {};     // Parse just one argument for testing file existence and permissions.
    if (tokenize_args(cmd, argvec, 1) > 0)
    {
        const char* cmdname = argvec[0];
        if (access(cmdname, X_OK) != 0)
        {
            if (access(cmdname, F_OK) != 0)
            {
                MXB_ERROR("Cannot find file '%s'.", cmdname);
            }
            else
            {
                MXB_ERROR("Cannot execute file '%s'. Missing execution permission.", cmdname);
            }
        }
        else
        {
            success = true;
        }
        MXB_FREE(argvec[0]);
    }
    else
    {
        MXB_ERROR("Failed to parse argument string '%s' for external command.", cmd.c_str());
    }
    return success;
}
}

namespace maxbase
{
std::unique_ptr<ExternalCmd> ExternalCmd::create(const string& argstr, int timeout, OutputHandler handler)
{
    return ::check_executable(argstr) ?
           std::unique_ptr<ExternalCmd>(new ExternalCmd(argstr, timeout, std::move(handler))) : nullptr;
}

ExternalCmd::ExternalCmd(const std::string& script, int timeout, OutputHandler handler)
    : Process(Info(), 1000 * timeout)
    , m_orig_command(script)
    , m_subst_command(script)
    , m_handler(std::move(handler))
{
}

Process::~Process()
{
    if (m_proc_info.read_fd != -1)
    {
        close(m_proc_info.read_fd);
    }

    if (m_proc_info.write_fd != -1)
    {
        close(m_proc_info.write_fd);
    }

    if (m_proc_info.pid != -1)
    {
        wait();
        mxb_assert(m_proc_info.pid == -1);
    }
}

int ExternalCmd::run()
{
    return start() ? wait() : -1;
}

std::optional<Process::Info>
Process::start_external_cmd(const string& cmd, RedirStdErr redirect, ForkType fork_type)
{
    return (fork_type == ForkType::FORK) ? fork_external_cmd(cmd, redirect) :
           spawn_external_cmd(cmd, redirect);
}

std::optional<Process::Info> Process::fork_external_cmd(const string& cmd, Process::RedirStdErr redirect)
{
    // Create a pipe where the command can print output
    int read_fd[2];
    int write_fd[2];
    if (pipe(read_fd) == -1)
    {
        MXB_ERROR("Failed to open pipe: [%d] %s", errno, mxb_strerror(errno));
        return {};
    }
    else if (pipe(write_fd) == -1)
    {
        close(read_fd[0]);
        close(read_fd[1]);
        MXB_ERROR("Failed to open pipe: [%d] %s", errno, mxb_strerror(errno));
        return {};
    }

    // "execvp" takes its arguments as an array of tokens where the first element is the command.
    const int MAX_ARGS{256};
    char* argvec[MAX_ARGS + 1]{};
    ::tokenize_args(cmd, argvec, MAX_ARGS);
    const char* exec_name = argvec[0];

    // The SIGCHLD handler must be disabled before child process is forked,
    // otherwise we'll get an error
    pid_t pid = fork();
    if (pid < 0)
    {
        close(read_fd[0]);
        close(read_fd[1]);
        close(write_fd[0]);
        close(write_fd[1]);
        MXB_ERROR("Failed to execute command '%s', fork failed: [%d] %s",
                  exec_name, errno, mxb_strerror(errno));
        return {};
    }
    else if (pid == 0)
    {
        // This is the child process. Close the read end of the pipe and redirect
        // both stdout and stderr to the write end of the pipe
        close(read_fd[0]);
        dup2(read_fd[1], STDOUT_FILENO);
        if (redirect == RedirStdErr::YES)
        {
            dup2(read_fd[1], STDERR_FILENO);
        }

        // Close the write end of the other pipe and redirect the read end to the stdin of the command.
        close(write_fd[1]);
        dup2(write_fd[0], STDIN_FILENO);

        // Execute the command
        execvp(exec_name, argvec);

        // This is only reached if execvp failed to start the command. Print to the standard error stream.
        // The message will be caught by the parent process.
        int error = errno;
        if (error == EACCES)
        {
            // This is the most likely error, handle separately.
            fprintf(stderr, "error: Cannot execute file. File cannot be accessed or it is missing "
                            "execution permission.");
        }
        else
        {
            fprintf(stderr, "error: Cannot execute file. 'execvp' error: '%s'", strerror(error));
        }
        fflush(stderr);
        // Exit with error. The write end of the pipe should close by itself.
        _exit(1);
    }

    // Close the write end of the pipe and make the read end non-blocking
    close(read_fd[1]);
    fcntl(read_fd[0], F_SETFL, O_NONBLOCK);

    // Same for the write pipe but close the read end and make the write end non-blocking
    close(write_fd[0]);
    fcntl(write_fd[1], F_SETFL, O_NONBLOCK);

    MXB_INFO("Executing command '%s' in process %d", exec_name, pid);
    auto rval = std::optional<Info>({pid, read_fd[0], write_fd[1], exec_name});

    // Free the token array.
    for (int i = 0; i < MAX_ARGS && argvec[i]; i++)
    {
        MXB_FREE(argvec[i]);
    }

    return rval;
}

std::optional<Process::Info> Process::spawn_external_cmd(const string& cmd, Process::RedirStdErr redirect)
{
    // Create two pipes to communicate with the external process.
    const char pipe_fail[] = "Failed to open pipe: [%d] %s";
    int c_to_p[2];
    int p_to_c[2];
    if (pipe(c_to_p) == -1)
    {
        MXB_ERROR(pipe_fail, errno, mxb_strerror(errno));
        return {};
    }
    else if (pipe(p_to_c) == -1)
    {
        close(c_to_p[0]);
        close(c_to_p[1]);
        MXB_ERROR(pipe_fail, errno, mxb_strerror(errno));
        return {};
    }

    // "execvp" takes its arguments as an array of tokens where the first element is the command.
    const int MAX_ARGS{256};
    char* argvec[MAX_ARGS + 1]{};
    ::tokenize_args(cmd, argvec, MAX_ARGS);
    const char* exec_name = argvec[0];

    posix_spawn_file_actions_t file_actions;
    int rc = posix_spawn_file_actions_init(&file_actions);
    // posix_spawn_file_actions_init should not fail.
    mxb_assert(!rc);
    // Instruct child process to immediately close non-used fd:s and redirect standard streams.
    rc = posix_spawn_file_actions_addclose(&file_actions, c_to_p[0])
        || posix_spawn_file_actions_adddup2(&file_actions, c_to_p[1], STDOUT_FILENO)
        || posix_spawn_file_actions_addclose(&file_actions, p_to_c[1])
        || posix_spawn_file_actions_adddup2(&file_actions, p_to_c[0], STDIN_FILENO)
        || (redirect == RedirStdErr::YES
            && posix_spawn_file_actions_adddup2(&file_actions, c_to_p[1], STDERR_FILENO));

    std::optional<Info> rval;
    if (rc == 0)
    {
        pid_t pid = -1;
        rc = posix_spawn(&pid, exec_name, &file_actions, NULL, argvec, NULL);
        mxb_assert((rc == 0 && pid > 0) || (rc != 0 && pid == -1));
        if (rc == 0)
        {
            // Set remaining pipe fd:s non-blocking.
            fcntl(c_to_p[0], F_SETFL, O_NONBLOCK);
            fcntl(p_to_c[1], F_SETFL, O_NONBLOCK);

            MXB_INFO("Executing command '%s' in process %d", exec_name, pid);
            rval = std::optional<Info>({pid, c_to_p[0], p_to_c[1], exec_name});
        }
        else
        {
            MXB_ERROR("Failed to execute command '%s', posix_spawn failed. Error %i: %s",
                      exec_name, rc, mxb_strerror(rc));
        }
    }
    else
    {
        MXB_ERROR("posix_spawn_file_actions error. Error %i: %s", rc, mxb_strerror(rc));
    }
    posix_spawn_file_actions_destroy(&file_actions);

    // Regardless of success, close the write end of the child-to-parent pipe and read end of
    // the parent-to-child pipe.
    close(c_to_p[1]);
    close(p_to_c[0]);

    if (!rval.has_value())
    {
        // Fail, close remaining pipe fd:s.
        close(c_to_p[0]);
        close(p_to_c[1]);
    }

    // Free the token array.
    for (int i = 0; i < MAX_ARGS && argvec[i]; i++)
    {
        MXB_FREE(argvec[i]);
    }
    return rval;
}

bool ExternalCmd::start()
{
    return start_set_external_cmd(m_subst_command, RedirStdErr::YES);
}

bool ExternalCmd::write(const void* ptr, int64_t len)
{
    auto write_fd = proc_info().write_fd;
    mxb_assert(write_fd != -1);
    const uint8_t* p = reinterpret_cast<const uint8_t*>(ptr);
    auto start = mxb::Clock::now();

    pollfd pfd;
    pfd.fd = write_fd;
    pfd.events = POLLOUT;

    while (len > 0)
    {
        // Always try to read some output before writing.
        read_output();

        auto rc = ::write(write_fd, p, len);

        if (rc == -1)
        {
            if (errno == EAGAIN)
            {
                if (mxb::Clock::now() - start > std::chrono::seconds(timeout_ms() / 1000))
                {
                    MXB_ERROR("Write to pipe timed out");
                    return false;
                }

                if (poll(&pfd, 1, 1000) == -1)
                {
                    MXB_ERROR("Failed to poll pipe file descriptor: %d, %s", errno, mxb_strerror(errno));
                    return false;
                }
            }
            else
            {
                MXB_ERROR("Failed to write to pipe: %d, %s", errno, mxb_strerror(errno));
                return false;
            }
        }
        else if (rc > 0)
        {
            p += rc;
            len -= rc;
        }
        else
        {
            // Zero bytes, the socket is closed
            return false;
        }
    }

    return true;
}

void Process::close_output()
{
    auto& fd = m_proc_info.write_fd;
    if (fd != -1)
    {
        close(fd);
        fd = -1;
    }
}

int Process::try_wait()
{
    if (m_proc_info.pid != -1)
    {
        int exit_status;

        switch (waitpid(m_proc_info.pid, &exit_status, WNOHANG))
        {
        case -1:
            MXB_ERROR("Failed to wait for child process: %d, %s", errno, mxb_strerror(errno));
            m_result = ERROR;
            m_proc_info.pid = -1;
            break;

        case 0:
            m_result = TIMEOUT;
            break;

        default:
            m_proc_info.pid = -1;

            if (WIFEXITED(exit_status))
            {
                m_result = WEXITSTATUS(exit_status);
            }
            else if (WIFSIGNALED(exit_status))
            {
                m_result = WTERMSIG(exit_status);
            }
            else
            {
                MXB_ERROR("Command '%s' did not exit normally. Exit status: %d",
                          m_proc_info.exec_name.c_str(), exit_status);
                m_result = exit_status;
            }
            break;
        }
    }
    return m_result;
}

int ExternalCmd::try_wait()
{
    bool was_running = proc_info().pid != -1;
    auto result = Process::try_wait();

    if (was_running)
    {
        read_output();

        if (result != TIMEOUT && !m_output.empty())
        {
            m_handler(proc_info().exec_name, m_output);
        }
    }
    return result;
}

void ExternalCmd::read_output()
{
    int n;
    char buf[4096];             // This seems like enough space
    auto read_fd = proc_info().read_fd;

    while ((n = read(read_fd, buf, sizeof(buf))) > 0)
    {
        // Read all available output
        m_output.append(buf, n);

        for (size_t pos = m_output.find('\n'); pos != std::string::npos; pos = m_output.find('\n'))
        {
            if (pos == 0)
            {
                m_output.erase(0, 1);
            }
            else
            {
                std::string line = m_output.substr(0, pos);
                m_output.erase(0, pos + 1);
                m_handler(proc_info().exec_name, line);
            }
        }
    }
}

int Process::wait()
{
    bool first_warning = true;
    int t = 0;

    while (try_wait() == TIMEOUT)
    {
        if (t++ > m_timeout_ms)
        {
            // Command timed out
            t = 0;
            if (first_warning)
            {
                MXB_WARNING("Soft timeout for command '%s', sending SIGTERM", m_proc_info.exec_name.c_str());
                kill(m_proc_info.pid, SIGTERM);
                first_warning = false;
            }
            else
            {
                MXB_ERROR("Hard timeout for command '%s', sending SIGKILL", m_proc_info.exec_name.c_str());
                kill(m_proc_info.pid, SIGKILL);
            }
        }
        else
        {
            // Sleep and try again
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    return m_result;
}

void ExternalCmd::substitute_arg(const std::string& match, const std::string& replace)
{
    // The match may be in the subject multiple times. Find all locations.
    string::size_type next_search_begin = 0;
    while (next_search_begin < m_subst_command.length())
    {
        auto position = m_subst_command.find(match, next_search_begin);
        if (position == string::npos)
        {
            next_search_begin = m_subst_command.length();
        }
        else
        {
            m_subst_command.replace(position, match.length(), replace);
            next_search_begin = position + replace.length();
        }
    }
}

void ExternalCmd::match_substitute(const string& keyword, const std::function<string(void)>& generator)
{
    if (m_orig_command.find(keyword) != string::npos)
    {
        substitute_arg(keyword, generator());
    }
}

void ExternalCmd::reset_substituted()
{
    m_subst_command = m_orig_command;
}

const char* ExternalCmd::substituted() const
{
    return m_subst_command.c_str();
}

Process::Process(Info info, int timeout_ms)
    : m_proc_info(std::move(info))
    , m_timeout_ms(timeout_ms)
{
}

const Process::Info& Process::proc_info() const
{
    return m_proc_info;
}

int Process::timeout_ms() const
{
    return m_timeout_ms;
}

bool Process::start_set_external_cmd(const string& cmd, RedirStdErr redirect)
{
    bool rval = false;
    auto res = start_external_cmd(cmd, redirect, ForkType::FORK);
    if (res)
    {
        m_proc_info = std::move(*res);
        rval = true;
    }
    return rval;
}

std::unique_ptr<AsyncCmd> AsyncCmd::create(const string& cmd, int timeout_ms)
{
    return ::check_executable(cmd) ?
           std::unique_ptr<AsyncCmd>(new AsyncCmd(cmd, timeout_ms)) : nullptr;
}

AsyncCmd::AsyncCmd(const string& cmd, int timeout_ms)
    : m_cmd(cmd)
    , m_timeout_ms(timeout_ms)
{
}

std::unique_ptr<AsyncProcess> AsyncCmd::start()
{
    std::unique_ptr<AsyncProcess> rval;
    auto res = Process::start_external_cmd(m_cmd, Process::RedirStdErr::NO, Process::ForkType::SPAWN);
    if (res)
    {
        rval.reset(new AsyncProcess(std::move(*res), m_timeout_ms));
    }
    return rval;
}

AsyncProcess::AsyncProcess(Info info, int timeout_ms)
    : Process(std::move(info), timeout_ms)
{
}

std::optional<std::string> AsyncProcess::read_output()
{
    const ssize_t buflen = 4096;
    char buf[buflen];
    ssize_t n;
    auto read_fd = proc_info().read_fd;
    string output;

    while ((n = ::read(read_fd, buf, buflen)) > 0)
    {
        // Read all available output. No need to check for EINTR as the read is non-blocking.
        output.append(buf, n);
        if (n < buflen)
        {
            // Nothing more to read.
            break;
        }
    }

    std::optional<std::string> rval;
    if (!output.empty())
    {
        rval = std::move(output);
    }
    else if (n == -1 && errno == EAGAIN)
    {
        rval = "";
    }
    return rval;
}

bool AsyncProcess::write(const uint8_t* ptr, size_t len)
{
    auto write_fd = proc_info().write_fd;
    mxb_assert(write_fd != -1);
    bool rval = false;
    if (len > 0)
    {
        auto rc = ::write(write_fd, ptr, len);
        if (rc == (ssize_t)len)
        {
            rval = true;
        }
        else if (rc >= 0 || (rc == -1 && errno == EAGAIN))
        {
            // TODO: add write buffering if amount of data increases to this point.
            MXB_ERROR("Failed to write all data to pipe.");
            mxb_assert(!true);
        }
        else
        {
            MXB_ERROR("Failed to write to pipe: %d, %s", errno, mxb_strerror(errno));
        }
    }
    else
    {
        rval = true;
    }
    return rval;
}

int AsyncProcess::try_wait()
{
    return Process::try_wait();
}

int AsyncProcess::read_fd() const
{
    return proc_info().read_fd;
}
}
