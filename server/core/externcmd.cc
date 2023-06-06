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

#include <maxscale/externcmd.hh>

#include <ctype.h>
#include <errno.h>
#include <string.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <thread>

#include <maxbase/assert.hh>
#include <maxbase/alloc.hh>
#include <maxbase/string.hh>
#include <maxscale/pcre2.hh>

using std::string;

namespace
{
const char* skip_whitespace(const char* ptr)
{
    while (*ptr && isspace(*ptr))
    {
        ptr++;
    }

    return ptr;
}

const char* skip_prefix(const char* str)
{
    const char* ptr = strchr(str, ':');
    mxb_assert(ptr);

    ptr++;
    return skip_whitespace(ptr);
}

void log_output(const std::string& cmd, const std::string& str)
{
    int err;

    if (mxs_pcre2_simple_match("(?i)^[[:space:]]*alert[[:space:]]*[:]",
                               str.c_str(),
                               0,
                               &err) == MXS_PCRE2_MATCH)
    {
        MXB_ALERT("%s: %s", cmd.c_str(), skip_prefix(str.c_str()));
    }
    else if (mxs_pcre2_simple_match("(?i)^[[:space:]]*error[[:space:]]*[:]",
                                    str.c_str(),
                                    0,
                                    &err) == MXS_PCRE2_MATCH)
    {
        MXB_ERROR("%s: %s", cmd.c_str(), skip_prefix(str.c_str()));
    }
    else if (mxs_pcre2_simple_match("(?i)^[[:space:]]*warning[[:space:]]*[:]",
                                    str.c_str(),
                                    0,
                                    &err) == MXS_PCRE2_MATCH)
    {
        MXB_WARNING("%s: %s", cmd.c_str(), skip_prefix(str.c_str()));
    }
    else if (mxs_pcre2_simple_match("(?i)^[[:space:]]*notice[[:space:]]*[:]",
                                    str.c_str(),
                                    0,
                                    &err) == MXS_PCRE2_MATCH)
    {
        MXB_NOTICE("%s: %s", cmd.c_str(), skip_prefix(str.c_str()));
    }
    else if (mxs_pcre2_simple_match("(?i)^[[:space:]]*(info|debug)[[:space:]]*[:]",
                                    str.c_str(),
                                    0,
                                    &err) == MXS_PCRE2_MATCH)
    {
        MXB_INFO("%s: %s", cmd.c_str(), skip_prefix(str.c_str()));
    }
    else
    {
        // No special format, log as notice level message
        MXB_NOTICE("%s: %s", cmd.c_str(), skip_whitespace(str.c_str()));
    }
}
}

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
            else if (quoted && !escaped && *ptr == qc)      /** End of quoted string */
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
    std::unique_ptr<ExternalCmd> cmd(new ExternalCmd(argstr, timeout, handler));
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
    , m_handler(handler ? handler : log_output)
{
}

ExternalCmd::~ExternalCmd()
{
    if (m_pid != -1)
    {
        wait();
        mxb_assert(m_pid == -1);
    }

    if (m_read_fd != -1)
    {
        close(m_read_fd);
    }
}

int ExternalCmd::run()
{
    return start() ? wait() : -1;
}

bool ExternalCmd::start()
{
    // Create a pipe where the command can print output
    int fd[2];
    if (pipe(fd) == -1)
    {
        MXB_ERROR("Failed to open pipe: [%d] %s", errno, mxb_strerror(errno));
        return false;
    }

    // "execvp" takes its arguments as an array of tokens where the first element is the command.
    char* argvec[MAX_ARGS + 1] {};
    tokenize_args(argvec, MAX_ARGS);
    std::string m_cmd = argvec[0];

    // The SIGCHLD handler must be disabled before child process is forked,
    // otherwise we'll get an error
    pid_t pid = fork();
    if (pid < 0)
    {
        close(fd[0]);
        close(fd[1]);
        MXB_ERROR("Failed to execute command '%s', fork failed: [%d] %s",
                  m_cmd.c_str(), errno, mxb_strerror(errno));
        return false;
    }
    else if (pid == 0)
    {
        // This is the child process. Close the read end of the pipe and redirect
        // both stdout and stderr to the write end of the pipe
        close(fd[0]);
        dup2(fd[1], STDOUT_FILENO);
        dup2(fd[1], STDERR_FILENO);

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
    close(fd[1]);
    fcntl(fd[0], F_SETFL, O_NONBLOCK);

    m_pid = pid;
    m_read_fd = fd[0];

    // Free the token array.
    for (int i = 0; i < MAX_ARGS && argvec[i]; i++)
    {
        MXB_FREE(argvec[i]);
    }

    MXB_INFO("Executing command '%s' in process %d", m_cmd.c_str(), pid);

    return true;
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
            m_handler(m_cmd.c_str(), m_output);
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

        for (size_t pos = m_output.find("\n"); pos != std::string::npos; pos = m_output.find("\n"))
        {
            if (pos == 0)
            {
                m_output.erase(0, 1);
            }
            else
            {
                std::string line = m_output.substr(0, pos);
                m_output.erase(0, pos + 1);
                m_handler(m_cmd.c_str(), line);
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
