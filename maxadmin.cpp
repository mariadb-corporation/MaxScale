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

    char		buf[1024];

    char		*hostname = (char *) "192.168.122.105";
    char		*port = (char *) "6603";
    char		*user = (char *) "admin";
    char		*passwd = (char *) "skysql";
    int		so;

    if ((so = connectMaxScale(hostname, port)) == -1)
        exit(1);
    if (!authMaxScale(so, user, passwd))
    {
        fprintf(stderr, "Failed to connect to MaxScale. "
                "Incorrect username or password.\n");
        exit(1);
    }

    sendCommand(so, (char *) "show server server2", buf);

    printf("%s", buf);
    printf("%s", strstr(buf, "Slave delay:"));

    close(so);
    return 0;
}

