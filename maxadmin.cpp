/*
 * This file is distributed as part of MaxScale.  It is free
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
 * Copyright MariaDB Corporation Ab 2014
 */

#include "maxadmin_operations.h"

/**
 * @file maxadmin.c  - The MaxScale administration client
 *
 * @verbatim
 * Revision History
 *
 * Date		Who		Description
 * 13/06/14	Mark Riddoch	Initial implementation
 * 15/06/14	Mark Riddoch	Addition of source command
 * 26/06/14	Mark Riddoch	Fix issue with final OK split across
 *				multiple reads
 *
 * @endverbatim
 */



/**
 * The main for the maxadmin client
 *
 * @param argc	Number of arguments
 * @param argv	The command line arguments
 */
int
main(int argc, char **argv)
{


    char result[1024];

    get_maxadmin_param((char *) "192.168.122.105", (char *) "admin", (char *) "mariadb", (char *) "show server server2", (char *) "Port:", result);

    printf("%s\n", result);

    return 0;
}

