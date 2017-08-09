/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2019-07-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "maxscale/externcmd.h"

#include <ctype.h>
#include <errno.h>
#include <string.h>

#include <maxscale/alloc.h>
#include <maxscale/log_manager.h>
#include <maxscale/pcre2.h>



/**
 * Tokenize a string into arguments suitable for a execvp call.
 * @param args Argument string
 * @param argv Array of char pointers to be filled with tokenized arguments
 * @return 0 on success, -1 on error
 */
int tokenize_arguments(char* argstr, char** argv)
{
    int i = 0;
    bool quoted = false;
    bool read = false;
    bool escaped = false;
    char *ptr, *start;
    char args[strlen(argstr) + 1];
    char qc = 0;

    strcpy(args, argstr);
    start = args;
    ptr = start;

    while (*ptr != '\0' && i < MAXSCALE_EXTCMD_ARG_MAX)
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
            else if (quoted && !escaped && *ptr == qc) /** End of quoted string */
            {
                *ptr = '\0';
                argv[i++] = MXS_STRDUP_A(start);
                read = false;
                quoted = false;
            }
            else if (!quoted)
            {
                if (isspace(*ptr))
                {
                    *ptr = '\0';
                    if (read) /** New token */
                    {
                        argv[i++] = MXS_STRDUP_A(start);
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
        argv[i++] = MXS_STRDUP_A(start);
    }

    argv[i] = NULL;

    return 0;
}

/**
 * Allocate a new external command.
 * The name and parameters are copied into the external command structure so
 * the original memory can be freed if needed.
 * @param command Command to execute with the parameters
 * @return Pointer to new external command struct or NULL if an error occurred
 */
EXTERNCMD* externcmd_allocate(char* argstr)
{
    EXTERNCMD* cmd = (EXTERNCMD*) MXS_MALLOC(sizeof(EXTERNCMD));
    char** argv = (char**) MXS_MALLOC(sizeof(char*) * MAXSCALE_EXTCMD_ARG_MAX);

    if (argstr && cmd && argv)
    {
        cmd->argv = argv;
        if (tokenize_arguments(argstr, cmd->argv) == 0)
        {
            if (access(cmd->argv[0], X_OK) != 0)
            {
                if (access(cmd->argv[0], F_OK) != 0)
                {
                    MXS_ERROR("Cannot find file: %s", cmd->argv[0]);
                }
                else
                {
                    MXS_ERROR("Cannot execute file '%s'. Missing "
                              "execution permissions.", cmd->argv[0]);
                }
                externcmd_free(cmd);
                cmd = NULL;
            }
        }
        else
        {
            MXS_ERROR("Failed to parse argument string for external command: %s",
                      argstr);
            externcmd_free(cmd);
            cmd = NULL;
        }
    }
    else
    {
        MXS_FREE(cmd);
        MXS_FREE(argv);
        cmd = NULL;
    }
    return cmd;
}

/**
 * Free a previously allocated external command.
 * @param cmd Command to free
 */
void externcmd_free(EXTERNCMD* cmd)
{
    if (cmd)
    {
        for (int i = 0; cmd->argv[i]; i++)
        {
            MXS_FREE(cmd->argv[i]);
        }
        MXS_FREE(cmd->argv);
        MXS_FREE(cmd);
    }
}

/**
 *Execute a command in a separate process.
 *@param cmd Command to execute
 *@return 0 on success, -1 on error.
 */
int externcmd_execute(EXTERNCMD* cmd)
{
    int rval = 0;
    pid_t pid;

    pid = fork();

    if (pid < 0)
    {
        char errbuf[MXS_STRERROR_BUFLEN];
        MXS_ERROR("Failed to execute command '%s', fork failed: [%d] %s",
                  cmd->argv[0], errno, strerror_r(errno, errbuf, sizeof(errbuf)));
        rval = -1;
    }
    else if (pid == 0)
    {
        /** Child process, execute command */
        execvp(cmd->argv[0], cmd->argv);
        _exit(1);
    }
    else
    {
        cmd->child = pid;
        cmd->n_exec++;
        MXS_DEBUG("[monitor_exec_cmd] Forked child process %d : %s.", pid, cmd->argv[0]);
    }

    return rval;
}

/**
 * Substitute all occurrences of @c match with @c replace in the arguments for @c cmd.
 * @param cmd External command
 * @param match Match string
 * @param replace Replacement string
 * @return true if replacement was successful, false on error
 */
bool externcmd_substitute_arg(EXTERNCMD* cmd, const char* match, const char* replace)
{
    int err;
    bool rval = true;
    size_t errpos;
    pcre2_code *re = pcre2_compile((PCRE2_SPTR) match, PCRE2_ZERO_TERMINATED, 0, &err, &errpos, NULL);
    if (re)
    {
        for (int i = 0; cmd->argv[i] && rval; i++)
        {
            size_t size = strlen(cmd->argv[i]);
            char* dest = MXS_MALLOC(size);
            if (dest)
            {
                mxs_pcre2_result_t rc = mxs_pcre2_substitute(re, cmd->argv[i], replace, &dest, &size);
                switch (rc)
                {
                case MXS_PCRE2_ERROR:
                    MXS_FREE(dest);
                    rval = false;
                    break;
                case MXS_PCRE2_MATCH:
                    MXS_FREE(cmd->argv[i]);
                    cmd->argv[i] = dest;
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
char* get_command(const char* str)
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
        rval = MXS_MALLOC(len + 1);

        if (rval)
        {
            memcpy(rval, start, len);
            rval[len] = '\0';
        }
    }

    return rval;
}

/**
 * Check if a command can be executed.
 *
 * Checks if the file being executed exists and if the current user has execution
 * permissions on the file.
 * @param argstr Command to check. Can contain arguments for the command.
 * @return True if the file was found and the use has execution permissions to it.
 */
bool externcmd_can_execute(const char* argstr)
{
    bool rval = false;
    char *command = get_command(argstr);

    if (command)
    {
        if (access(command, X_OK) == 0)
        {
            rval = true;
        }
        else if (access(command, F_OK) == 0)
        {
            MXS_ERROR("The executable cannot be executed: %s", command);
        }
        else
        {
            MXS_ERROR("The executable cannot be found: %s", command);
        }
        MXS_FREE(command);
    }
    return rval;
}

/**
 * Simple matching of string and command
 * @param cmd Command where the match is searched from
 * @param match String to search for
 * @return True if the string matched
 */
bool externcmd_matches(const EXTERNCMD* cmd, const char* match)
{
    for (int i = 0; cmd->argv[i]; i++)
    {
        if (strstr(cmd->argv[i], match))
        {
            return true;
        }
    }

    return false;
}
