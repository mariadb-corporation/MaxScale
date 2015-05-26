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

/**
 * @file maxkeys.c  - Create the random encryption keys for maxscale
 *
 * @verbatim
 * Revision History
 *
 * Date		Who		Description
 * 24/07/13	Mark Riddoch	Initial implementation
 *
 * @endverbatim
 */
#include	<stdio.h>
#include	<secrets.h>
#include <skygw_utils.h>
#include <log_manager.h>
#include <gwdirs.h>
int main(int argc, char **argv)
{
    int arg_count = 6;
    char *home;
    char** arg_vector;
    int rval = 0;

	if (argc != 2)
	{
		fprintf(stderr, "Usage: %s <filename>\n", argv[0]);
		return 1;
	}

	arg_vector = malloc(sizeof(char*)*(arg_count + 1));

	if(arg_vector == NULL)
	{
	    fprintf(stderr,"Error: Memory allocation failed.\n");
	    return 1;
	}

	if(access("/var/log/maxscale/maxkeys/",F_OK) != 0)
	{
            if(mkdir("/var/log/maxscale/maxkeys/",0777) == -1)
            {
		if(errno != EEXIST)
		{
		    fprintf(stderr,"Error: %d - %s",errno,strerror(errno));
		    return 1;
		}
            }
	}
	arg_vector[0] = strdup("logmanager");
	arg_vector[1] = strdup("-j");
	arg_vector[2] = strdup("/var/log/maxscale/maxkeys");
	arg_vector[3] = NULL;
	skygw_logmanager_init(arg_count,arg_vector);
	free(arg_vector[2]);
	free(arg_vector);
	

	if (secrets_writeKeys(argv[1]))
	{
		fprintf(stderr, "Failed to encode the password\n");
		rval = 1;
	}

	skygw_log_sync_all();
	skygw_logmanager_done();

    return rval;
}
