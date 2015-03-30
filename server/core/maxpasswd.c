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
 * @file maxpasswd.c  - Implementation of pasword encoding
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
/**
 * Encrypt a password for storing in the MaxScale.cnf file
 *
 * @param argc	Argument count
 * @param argv	Argument vector
 */
int
main(int argc, char **argv)
{
	char	*enc, *pw;
	int	arg_count = 3;
	char	*home;
    char** arg_vector;
	

	if (argc != 2)
	{
		fprintf(stderr, "Usage: %s <password>\n", argv[0]);
		exit(1);
	}

    arg_vector = malloc(sizeof(char*)*4);

	if(arg_vector == NULL)
	{
	    fprintf(stderr,"Error: Memory allocation failed.\n");
	    return 1;
	}

	arg_vector[0] = strdup("logmanager");
	arg_vector[1] = strdup("-j");

	if ((home = getenv("MAXSCALE_HOME")) != NULL)
	{
	    arg_vector[2] = (char*)malloc((strlen(home) + strlen("/log"))*sizeof(char));
	    sprintf(arg_vector[2],"%s/log",home);
	}
	else
	{
	    arg_vector[2] = strdup("/usr/local/mariadb-maxscale/log");
	}

	arg_vector[3] = NULL;
	skygw_logmanager_init(arg_count,arg_vector);
	skygw_log_enable(LOGFILE_TRACE);
	skygw_log_enable(LOGFILE_DEBUG);
	free(arg_vector[0]);
	free(arg_vector[1]);
	free(arg_vector[2]);
	free(arg_vector);
	
	pw = calloc(81,sizeof(char));

	if(pw == NULL){
		fprintf(stderr, "Error: cannot allocate enough memory.");
		exit(1);
	}

	strncpy(pw,argv[1],80);

	if ((enc = encryptPassword(pw)) != NULL){
		printf("%s\n", enc);
	}else{
		fprintf(stderr, "Failed to encode the password\n");
	}

	free(pw);
	skygw_log_sync_all();
	skygw_logmanager_done();
	return 0;
}
