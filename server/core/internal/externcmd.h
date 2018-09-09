/*
 * Copyright (c) 2018 MariaDB Corporation Ab
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
#pragma once

#include <maxscale/cdefs.h>

#include <unistd.h>

MXS_BEGIN_DECLS

#define MAXSCALE_EXTCMD_ARG_MAX 256

typedef struct extern_cmd_t
{
    char** argv;        /**< Argument vector for the command, first being the
                         * actual command being executed */
    int      n_exec;    /**< Number of times executed */
    pid_t    child;     /**< PID of the child process */
    uint32_t timeout;   /**< Command timeout in seconds */
} EXTERNCMD;

char* externcmd_extract_command(const char* argstr);

/**
 * Allocate a new external command.
 *
 * The name and parameters are copied into the external command structure so
 * the original memory can be freed if needed.
 *
 * @param command Command to execute with the parameters
 * @param timeout Command timeout in seconds
 *
 * @return Pointer to new external command struct or NULL if an error occurred
 */
EXTERNCMD* externcmd_allocate(const char* argstr, uint32_t timeout);


/**
 * Free a previously allocated external command
 *
 * @param cmd Command to free
 */
void externcmd_free(EXTERNCMD* cmd);

/**
 * Execute a command
 *
 * The output of the command must be freed by the caller by calling MXS_FREE.
 *
 * @param cmd  Command to execute
 *
 * @return The return value of the executed command or -1 on error
 */
int externcmd_execute(EXTERNCMD* cmd);

/**
 * Substitute all occurrences of @c match with @c replace in the arguments for @c cmd
 *
 * @param cmd External command
 * @param match Match string
 * @param replace Replacement string
 *
 * @return True if replacement was successful, false on error
 */
bool externcmd_substitute_arg(EXTERNCMD* cmd, const char* re, const char* replace);

/**
 * Check if a command can be executed
 *
 * Checks if the file being executed exists and if the current user has execution
 * permissions on the file.
 *
 * @param argstr Command to check, can contain arguments for the command
 *
 * @return True if the file was found and the use has execution permissions to it
 */
bool externcmd_can_execute(const char* argstr);

/**
 * Simple matching of string and command
 *
 * @param cmd Command where the match is searched from
 * @param match String to search for
 *
 * @return True if the string matched
 */
bool externcmd_matches(const EXTERNCMD* cmd, const char* match);

MXS_END_DECLS
