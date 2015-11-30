#ifndef _EXTERN_CMD_HG
#define _EXTERN_CMD_HG
/*
 * This file is distributed as part of the MariaDB Corporation MaxScale.  It is free
 * software: you can redistribute it and/or modify it under the terms of the
 * GNU General Public License as published by the Free Software Foundation,
 * version 2.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Copyright MariaDB Corporation Ab 2013-2014
 */

#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <skygw_utils.h>
#include <log_manager.h>
#include <maxscale_pcre2.h>

#define MAXSCALE_EXTCMD_ARG_MAX 256

typedef struct extern_cmd_t
{
  char** argv; /*< Argument vector for the command, first being the actual command
                * being executed. */
  int n_exec; /*< Number of times executed */
  pid_t child; /*< PID of the child process */
}EXTERNCMD;

char* externcmd_extract_command(const char* argstr);
EXTERNCMD* externcmd_allocate(char* argstr);
void externcmd_free(EXTERNCMD* cmd);
int externcmd_execute(EXTERNCMD* cmd);
bool externcmd_substitute_arg(EXTERNCMD* cmd, const char* re, const char* replace);
bool externcmd_can_execute(const char* argstr);

#endif
