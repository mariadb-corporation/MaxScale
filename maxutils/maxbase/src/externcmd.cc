/*
 * Copyright (c) 2016 MariaDB Corporation Ab
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

#include <maxbase/externcmd.hh>

#include <ctype.h>
#include <errno.h>
#include <string.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <poll.h>
#include <thread>

#include <maxbase/assert.hh>
#include <maxbase/alloc.hh>
#include <maxbase/string.hh>
#include <maxbase/stopwatch.hh>

using std::string;

namespace maxbase
{
int ExternalCmd::tokenize_args(char* dest[], int dest_size)
{
    bool quoted = false;
    bool read = false;
    bool escaped = false;
    char qc = 0;

    char args[m_subst_command.length() + 1];
    strcpy(args, m_subst_command.c_str());
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

std::unique_ptr<ExternalCmd> ExternalCmd::create(const string& argstr, int timeout, OutputHandler handler)
{
    bool success = false;
    std::unique_ptr<ExternalCmd> cmd(new ExternalCmd(argstr, timeout, std::move(handler)));
    char* argvec[1] {};     // Parse just one argument for testing file existence and permissions.
    if (cmd->tokenize_args(argvec, 1) > 0)
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
        MXB_ERROR("Failed to parse argument string '%s' for external command.", argstr.c_str());
    }

    if (!success)
    {
        cmd.reset();
    }
    return cmd;
}

ExternalCmd::ExternalCmd(const std::string& script, int timeout, OutputHandler handler)
    : m_orig_command(script)
    , m_subst_command(script)
    , m_timeout(timeout)
    , m_handler(std::move(handler))
{
}

ExternalCmd::~ExternalCmd()
{
    if (m_read_fd != -1)
    {
        close(m_read_fd);
    }

    if (m_write_fd != -1)
    {
        close(m_write_fd);
    }

    if (m_pid != -1)
    {
        wait();
        mxb_assert(m_pid == -1);
    }
}

int ExternalCmd::run()
{
    return start() ? wait() : -1;
}

bool ExternalCmd::start()
{
    // Create a pipe where the command can print output
    int read_fd[2];
    int write_fd[2];
    if (pipe(read_fd) == -1)
    {
        MXB_ERROR("Failed to open pipe: [%d] %s", errno, mxb_strerror(errno));
        return false;
    }
    else if (pipe(write_fd) == -1)
    {
        close(read_fd[0]);
        close(read_fd[1]);
        MXB_ERROR("Failed to open pipe: [%d] %s", errno, mxb_strerror(errno));
        return false;
    }

    // "execvp" takes its arguments as an array of tokens where the first element is the command.
    char* argvec[MAX_ARGS + 1] {};
    tokenize_args(argvec, MAX_ARGS);
    m_cmd = argvec[0];

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
                  m_cmd.c_str(), errno, mxb_strerror(errno));
        return false;
    }
    else if (pid == 0)
    {
        // This is the child process. Close the read end of the pipe and redirect
        // both stdout and stderr to the write end of the pipe
        close(read_fd[0]);
        dup2(read_fd[1], STDOUT_FILENO);
        dup2(read_fd[1], STDERR_FILENO);

        // Close the write end of the other pipe and redirect the read end to the stdin of the command.
        close(write_fd[1]);
        dup2(write_fd[0], STDIN_FILENO);

        // Execute the command
        execvp(argvec[0], argvec);

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

    m_pid = pid;
    m_read_fd = read_fd[0];
    m_write_fd = write_fd[1];

    // Free the token array.
    for (int i = 0; i < MAX_ARGS && argvec[i]; i++)
    {
        MXB_FREE(argvec[i]);
    }

    MXB_INFO("Executing command '%s' in process %d", m_cmd.c_str(), pid);

    return true;
}

bool ExternalCmd::write(const void* ptr, int64_t len)
{
    mxb_assert(m_write_fd != -1);
    const uint8_t* p = reinterpret_cast<const uint8_t*>(ptr);
    auto start = mxb::Clock::now();

    pollfd pfd;
    pfd.fd = m_write_fd;
    pfd.events = POLLOUT;

    while (len > 0)
    {
        // Always try to read some output before writing.
        read_output();

        auto rc = ::write(m_write_fd, p, len);

        if (rc == -1)
        {
            if (errno == EAGAIN)
            {
                if (mxb::Clock::now() - start > std::chrono::seconds(m_timeout))
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

void ExternalCmd::close_output()
{
    if (m_write_fd != -1)
    {
        close(m_write_fd);
        m_write_fd = -1;
    }
}

int ExternalCmd::try_wait()
{
    if (m_pid != -1)
    {
        int exit_status;

        switch (waitpid(m_pid, &exit_status, WNOHANG))
        {
        case -1:
            MXB_ERROR("Failed to wait for child process: %d, %s", errno, mxb_strerror(errno));
            m_result = ERROR;
            m_pid = -1;
            break;

        case 0:
            m_result = TIMEOUT;
            break;

        default:
            m_pid = -1;

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
                MXB_ERROR("Command '%s' did not exit normally. Exit status: %d", m_cmd.c_str(), exit_status);
                m_result = exit_status;
            }
            break;
        }

        read_output();

        if (m_result != TIMEOUT && !m_output.empty())
        {
            m_handler(m_cmd, m_output);
        }
    }

    return m_result;
}

void ExternalCmd::read_output()
{
    int n;
    char buf[4096];             // This seems like enough space

    while ((n = read(m_read_fd, buf, sizeof(buf))) > 0)
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
                m_handler(m_cmd, line);
            }
        }
    }
}

int ExternalCmd::wait()
{
    string output;
    bool first_warning = true;
    uint64_t t = 0;
    uint64_t t_max = m_timeout * 1000;

    while (try_wait() == TIMEOUT)
    {
        if (t++ > t_max)
        {
            // Command timed out
            t = 0;
            if (first_warning)
            {
                MXB_WARNING("Soft timeout for command '%s', sending SIGTERM", m_cmd.c_str());
                kill(m_pid, SIGTERM);
                first_warning = false;
            }
            else
            {
                MXB_ERROR("Hard timeout for command '%s', sending SIGKILL", m_cmd.c_str());
                kill(m_pid, SIGKILL);
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
}
