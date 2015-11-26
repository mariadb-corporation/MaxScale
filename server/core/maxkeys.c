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
 * Date         Who             Description
 * 24/07/13     Mark Riddoch    Initial implementation
 *
 * @endverbatim
 */
#include <stdio.h>
#include <secrets.h>
#include <skygw_utils.h>
#include <log_manager.h>
#include <gwdirs.h>

int main(int argc, char **argv)
{
    char *keyfile;
    int rval = 0;

    if (argc < 2)
    {
        keyfile = "/var/lib/maxscale/";
        fprintf(stderr, "Generating .secrets file in /var/lib/maxscale/ ...\n");
    }
    else
    {
        keyfile = argv[1];
    }

    mxs_log_init(NULL, NULL, MXS_LOG_TARGET_DEFAULT);

    if (secrets_writeKeys(keyfile))
    {
        fprintf(stderr, "Failed to encode the password\n");
        rval = 1;
    }

    mxs_log_flush_sync();
    mxs_log_finish();

    return rval;
}
