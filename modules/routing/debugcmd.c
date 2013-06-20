/*
 * This file is distributed as part of the SkySQL Gateway.  It is free
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
 * Copyright SkySQL Ab 2013
 */

/**
 * @file debugcmd.c  - The debug CLI command line interpreter
 *
 * @verbatim
 * Revision History
 *
 * Date		Who		Description
 * 20/06/13	Mark Riddoch	Initial implementation
 *
 * @endverbatim
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <service.h>
#include <session.h>
#include <router.h>
#include <modules.h>
#include <atomic.h>
#include <spinlock.h>
#include <dcb.h>
#include <poll.h>
#include <debugcli.h>

#define	MAXARGS	5

/**
 * The subcommand structure
 *
 * These are the options that may be passed to a command
 */
struct subcommand {
	char	*arg1;
	int	n_args;
	void	(*fn)();
	char	*help;
};

struct subcommand showoptions[] = {
	{ "sessions",	0, dprintAllSessions, 	"Show all active sessions in the gateway" },
	{ "services",	0, dprintAllServices,	"Show all configured services in the gateway" },
	{ "servers",	0, dprintAllServers,	"Show all configured servers" },
	{ "modules",	0, dprintAllModules,	"Show all currently loaded modules" },
	{ "dcbs",	0, dprintAllDCBs,	"Show all descriptor control blocks (network connections)" },
	{ "dcb",	1, dprintDCB,		"Show a single descriptor control block e.g. show dcb 0x493340" },
	{ "epoll",	0, dprintPollStats,	"Show the poll statistics" },
	{ NULL,		0, NULL,		NULL }
};

/**
 * The debug command table
 */
static struct {
	char			*cmd;
	struct	subcommand	*options;
} cmds[] = {
	{ "show",	showoptions },
	{ NULL,		NULL	}
};


static long
convert_arg(char *arg)
{
	return strtol(arg, NULL, 0);
}

/**
 * We have a complete line from the user, lookup the commands and execute them
 *
 * @param cli		The CLI_SESSION
 * @return	Returns 0 if the interpreter should exit
 */
int
execute_cmd(CLI_SESSION *cli)
{
DCB	*dcb = cli->session->client;
int	argc, i, j, found = 0;
char	*args[MAXARGS];
char	*saveptr, *delim = " \t\r\n";

	/* Tokenize the input string */
	args[0] = strtok_r(cli->cmdbuf, delim, &saveptr);
	i = 0;
	do {
		i++;
		args[i] = strtok_r(NULL, delim, &saveptr);
	} while (args[i] != NULL && i < MAXARGS);

	if (args[0] == NULL)
		return 1;
	argc = i - 2;	/* The number of extra arguments to commands */
	

	if (!strcasecmp(args[0], "help"))
	{
		if (args[1] == NULL)
		{
			found = 1;
			dcb_printf(dcb, "Available commands:\n");
			for (i = 0; cmds[i].cmd; i++)
			{
				for (j = 0; cmds[i].options[j].arg1; j++)
				{
					dcb_printf(dcb, "    %s %s\n", cmds[i].cmd, cmds[i].options[j].arg1);
				}
			}
		}
		else
		{
			for (i = 0; cmds[i].cmd; i++)
			{
				if (!strcasecmp(args[1], cmds[i].cmd))
				{
					found = 1;
					dcb_printf(dcb, "Available options to the %s command:\n", args[1]);
					for (j = 0; cmds[i].options[j].arg1; j++)
					{
						dcb_printf(dcb, "    %-10s %s\n", cmds[i].options[j].arg1,
										cmds[i].options[j].help);
					}
				}
			}
			if (found == 0)
			{
				dcb_printf(dcb, "No command %s to offer help with\n", args[1]);
			}
		}
		found = 1;
	}
	else if (!strcasecmp(args[0], "quit"))
	{
		return 0;
	}
	else
	{
		for (i = 0; cmds[i].cmd; i++)
		{
			if (strcasecmp(args[0], cmds[i].cmd) == 0)
			{
				for (j = 0; cmds[i].options[j].arg1; j++)
				{
					if (strcasecmp(args[1], cmds[i].options[j].arg1) == 0)
					{
						if (argc != cmds[i].options[j].n_args)
						{
							dcb_printf(dcb, "Incorrect number of arguments: %s %s expects %d arguments\n",
								cmds[i].cmd, cmds[i].options[j].arg1,
								cmds[i].options[j].n_args);
							
						}
						else
						{
							switch (cmds[i].options[j].n_args)
							{
							case 0:
								cmds[i].options[j].fn(dcb);
								break;
							case 1:
								cmds[i].options[j].fn(dcb, convert_arg(args[2]));
								break;
							case 2:
								cmds[i].options[j].fn(dcb, convert_arg(args[2]),
											convert_arg(args[3]));
								break;
							case 3:
								cmds[i].options[j].fn(dcb, convert_arg(args[2]),
											convert_arg(args[3]),
											convert_arg(args[4]));
							}
							found = 1;
						}
					}
				}
			}
		}
	}
	if (!found)
		dcb_printf(dcb,
			"Command not known, type help for a list of available commands\n");
	memset(cli->cmdbuf, 0, 80);

	return 1;
}
