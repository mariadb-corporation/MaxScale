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

/**
 * Encrypt a password for storing in the MaxScale.cnf file
 *
 * @param argc	Argument count
 * @param argv	Argument vector
 */
int
main(int argc, char **argv)
{
char	*enc;

	if (argc != 2)
	{
		fprintf(stderr, "Usage: %s <password>\n", argv[0]);
		exit(1);
	}
	
	if ((enc = encryptPassword(argv[1])) != NULL)
		printf("%s\n", enc);
	else
		fprintf(stderr, "Failed to encode the password\n");
	return 0;
}
