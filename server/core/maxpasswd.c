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
 * Date         Who             Description
 * 24/07/13     Mark Riddoch    Initial implementation
 *
 * @endverbatim
 */
#include <stdio.h>
#include <secrets.h>
#include <skygw_utils.h>
#include <log_manager.h>
/**
 * Encrypt a password for storing in the MaxScale.cnf file
 *
 * @param argc  Argument count
 * @param argv  Argument vector
 */
int
main(int argc, char **argv)
{
    char  *enc;
    char  *pw;
    char  *home;
    int    rval = 0;

    if (argc != 3)
    {
        fprintf(stderr, "Usage: %s <file> <password>\n", argv[0]);
        return 1;
    }

    mxs_log_init(NULL, NULL, MXS_LOG_TARGET_DEFAULT);

    pw = calloc(81, sizeof(char));

    if (pw == NULL)
    {
        fprintf(stderr, "Error: cannot allocate enough memory.");
        return 1;
    }

    strncpy(pw, argv[2], 80);

    if ((enc = encryptPassword(argv[1], pw)) != NULL)
    {
        printf("%s\n", enc);
    }
    else
    {
        fprintf(stderr, "Failed to encode the password\n");
        rval = 1;
    }

    free(pw);
    mxs_log_flush_sync();
    mxs_log_finish();
    return rval;
}
