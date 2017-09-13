#pragma once
/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2020-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/cdefs.h>

#include <unistd.h>

MXS_BEGIN_DECLS

#define MAXSCALE_EXTCMD_ARG_MAX 256

typedef struct extern_cmd_t
{
    char** argv;      /**< Argument vector for the command, first being the
                       * actual command being executed */
    int n_exec;       /**< Number of times executed */
    pid_t child;      /**< PID of the child process */
    uint32_t timeout; /**< Command timeout in seconds */
} EXTERNCMD;

char* externcmd_extract_command(const char* argstr);
EXTERNCMD* externcmd_allocate(char* argstr);
void externcmd_free(EXTERNCMD* cmd);
int externcmd_execute(EXTERNCMD* cmd);
bool externcmd_substitute_arg(EXTERNCMD* cmd, const char* re, const char* replace);
bool externcmd_can_execute(const char* argstr);
bool externcmd_matches(const EXTERNCMD* cmd, const char* match);

MXS_END_DECLS
