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

#include "internal/externcmd.hh"

#include <ctype.h>
#include <errno.h>
#include <string.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <thread>

#include <maxbase/assert.h>
#include <maxscale/alloc.h>
#include <maxscale/pcre2.h>

/**
 * Tokenize a string into arguments suitable for a `execvp` call.
 *
 * @param argstr Argument string
 * @return Number of arguments parsed from the string
 */
int ExternalCmd::tokenize_arguments(const char* argstr)
{
    int i = 0;
    bool quoted = false;
    bool read = false;
    bool escaped = false;
    char* ptr, * start;
    char args[strlen(argstr) + 1];
    char qc = 0;

    strcpy(args, argstr);
    start = args;
    ptr = start;

    while (*ptr != '\0' && i < MAX_ARGS)
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
                argv[i++] = MXS_STRDUP(start);
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
                        argv[i++] = MXS_STRDUP(start);
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
        argv[i++] = MXS_STRDUP(start);
    }

    argv[i] = nullptr;
    return i;
}

ExternalCmd* ExternalCmd::create(const char* argstr, int timeout)
{
    auto cmd = new ExternalCmd(timeout);
    bool success = false;
    if (argstr && cmd)
    {
        if (cmd->tokenize_arguments(argstr) > 0)
        {
            auto cmdname = cmd->argv[0];
            if (access(cmdname, X_OK) != 0)
            {
                if (access(cmdname, F_OK) != 0)
                {
                    MXS_ERROR("Cannot find file: %s", cmdname);
                }
                else
                {
                    MXS_ERROR("Cannot execute file '%s'. Missing execution permissions.", cmdname);
                }
            }
            else
            {
                success = true;
            }
        }
        else
        {
            MXS_ERROR("Failed to parse argument string for external command: %s", argstr);
        }
    }

    if (!success)
    {
        delete cmd;
        cmd = nullptr;
    }
    return cmd;
}

ExternalCmd::ExternalCmd(int timeout)
    : m_timeout(timeout)
{
}

ExternalCmd::~ExternalCmd()
{
    for (int i = 0; argv[i]; i++)
    {
        MXS_FREE(argv[i]);
    }
}

static const char* skip_whitespace(const char* ptr)
{
    while (*ptr && isspace(*ptr))
    {
        ptr++;
    }

    return ptr;
}

static const char* skip_prefix(const char* str)
{
    const char* ptr = strchr(str, ':');
    mxb_assert(ptr);

    ptr++;
    return skip_whitespace(ptr);
}

static void log_output(const char* cmd, const std::string& str)
{
    int err;

    if (mxs_pcre2_simple_match("(?i)^[[:space:]]*alert[[:space:]]*[:]",
                               str.c_str(),
                               0,
                               &err) == MXS_PCRE2_MATCH)
    {
        MXS_ALERT("%s: %s", cmd, skip_prefix(str.c_str()));
    }
    else if (mxs_pcre2_simple_match("(?i)^[[:space:]]*error[[:space:]]*[:]",
                                    str.c_str(),
                                    0,
                                    &err) == MXS_PCRE2_MATCH)
    {
        MXS_ERROR("%s: %s", cmd, skip_prefix(str.c_str()));
    }
    else if (mxs_pcre2_simple_match("(?i)^[[:space:]]*warning[[:space:]]*[:]",
                                    str.c_str(),
                                    0,
                                    &err) == MXS_PCRE2_MATCH)
    {
        MXS_WARNING("%s: %s", cmd, skip_prefix(str.c_str()));
    }
    else if (mxs_pcre2_simple_match("(?i)^[[:space:]]*notice[[:space:]]*[:]",
                                    str.c_str(),
                                    0,
                                    &err) == MXS_PCRE2_MATCH)
    {
        MXS_NOTICE("%s: %s", cmd, skip_prefix(str.c_str()));
    }
    else if (mxs_pcre2_simple_match("(?i)^[[:space:]]*(info|debug)[[:space:]]*[:]",
                                    str.c_str(),
                                    0,
                                    &err) == MXS_PCRE2_MATCH)
    {
        MXS_INFO("%s: %s", cmd, skip_prefix(str.c_str()));
    }
    else
    {
        // No special format, log as notice level message
        MXS_NOTICE("%s: %s", cmd, skip_whitespace(str.c_str()));
    }
}

int ExternalCmd::externcmd_execute()
{
    // Create a pipe where the command can print output
    int fd[2];

    if (pipe(fd) == -1)
    {
        MXS_ERROR("Failed to open pipe: [%d] %s", errno, mxs_strerror(errno));
        return -1;
    }

    int rval = 0;
    pid_t pid;
    const char* cmdname = argv[0];
    // The SIGCHLD handler must be disabled before child process is forked,
    // otherwise we'll get an error
    pid = fork();

    if (pid < 0)
    {
        close(fd[0]);
        close(fd[1]);
        MXS_ERROR("Failed to execute command '%s', fork failed: [%d] %s",
                  cmdname, errno, mxs_strerror(errno));
        rval = -1;
    }
    else if (pid == 0)
    {
        // This is the child process. Close the read end of the pipe and redirect
        // both stdout and stderr to the write end of the pipe
        close(fd[0]);
        dup2(fd[1], STDOUT_FILENO);
        dup2(fd[1], STDERR_FILENO);

        // Execute the command
        execvp(argv[0], argv);

        // Close the write end of the pipe and exit
        close(fd[1]);
        _exit(1);
    }
    else
    {
        MXS_INFO("Executing command '%s' in process %d", cmdname, pid);

        std::string output;
        bool first_warning = true;
        bool again = true;
        uint64_t t = 0;
        uint64_t t_max = m_timeout * 1000;

        // Close the write end of the pipe and make the read end non-blocking
        close(fd[1]);
        fcntl(fd[0], F_SETFL, O_NONBLOCK);

        while (again)
        {
            int exit_status;

            switch (waitpid(pid, &exit_status, WNOHANG))
            {
            case -1:
                MXS_ERROR("Failed to wait for child process: %d, %s", errno, mxs_strerror(errno));
                again = false;
                break;

            case 0:
                if (t++ > t_max)
                {
                    // Command timed out
                    t = 0;
                    if (first_warning)
                    {
                        MXS_WARNING("Soft timeout for command '%s', sending SIGTERM", cmdname);
                        kill(pid, SIGTERM);
                        first_warning = false;
                    }
                    else
                    {
                        MXS_ERROR("Hard timeout for command '%s', sending SIGKILL", cmdname);
                        kill(pid, SIGKILL);
                    }
                }
                else
                {
                    // Sleep and try again
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
                break;

            default:
                again = false;

                if (WIFEXITED(exit_status))
                {
                    rval = WEXITSTATUS(exit_status);
                }
                else if (WIFSIGNALED(exit_status))
                {
                    rval = WTERMSIG(exit_status);
                }
                else
                {
                    rval = exit_status;
                    MXS_ERROR("Command '%s' did not exit normally. Exit status: %d", cmdname, exit_status);
                }
                break;
            }

            int n;
            char buf[4096];     // This seems like enough space

            while ((n = read(fd[0], buf, sizeof(buf))) > 0)
            {
                // Read all available output
                output.append(buf, n);

                for (size_t pos = output.find("\n");
                     pos != std::string::npos; pos = output.find("\n"))
                {
                    if (pos == 0)
                    {
                        output.erase(0, 1);
                    }
                    else
                    {
                        std::string line = output.substr(0, pos);
                        output.erase(0, pos + 1);
                        log_output(cmdname, line);
                    }
                }
            }
        }

        if (!output.empty())
        {
            log_output(cmdname, output);
        }

        // Close the read end of the pipe and copy the data to the output parameter
        close(fd[0]);
    }

    return rval;
}

bool ExternalCmd::substitute_arg(const char* match, const char* replace)
{
    int err;
    bool rval = true;
    size_t errpos;
    pcre2_code* re = pcre2_compile((PCRE2_SPTR) match, PCRE2_ZERO_TERMINATED, 0, &err, &errpos, NULL);
    if (re)
    {
        for (int i = 0; argv[i] && rval; i++)
        {
            size_t size_orig = strlen(argv[i]);
            size_t size_replace = strlen(replace);
            size_t size = MXS_MAX(size_orig, size_replace);
            char* dest = (char*)MXS_MALLOC(size);
            if (dest)
            {
                mxs_pcre2_result_t rc = mxs_pcre2_substitute(re, argv[i], replace, &dest, &size);

                switch (rc)
                {
                case MXS_PCRE2_ERROR:
                    MXS_FREE(dest);
                    rval = false;
                    break;

                case MXS_PCRE2_MATCH:
                    MXS_FREE(argv[i]);
                    argv[i] = dest;
                    break;

                case MXS_PCRE2_NOMATCH:
                    MXS_FREE(dest);
                    break;
                }
            }
        }
        pcre2_code_free(re);
    }
    else
    {
        rval = false;
    }
    return rval;
}

/**
 * Get the name of the command being executed.
 *
 * This copies the command being executed into a new string.
 * @param str Command string, optionally with arguments
 * @return Command part of the string if arguments were defined
 */
static char* get_command(const char* str)
{
    char* rval = NULL;
    const char* start = str;

    while (*start && isspace(*start))
    {
        start++;
    }

    const char* end = start;

    while (*end && !isspace(*end))
    {
        end++;
    }

    size_t len = end - start;

    if (len > 0)
    {
        rval = (char*)MXS_MALLOC(len + 1);

        if (rval)
        {
            memcpy(rval, start, len);
            rval[len] = '\0';
        }
    }

    return rval;
}

bool ExternalCmd::externcmd_matches(const char* match)
{
    for (int i = 0; argv[i]; i++)
    {
        if (strstr(argv[i], match))
        {
            return true;
        }
    }

    return false;
}
