/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl.
 *
 * Change Date: 2019-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
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
    const char *keyfile;
    int rval = 0;

    if (argc < 2)
    {
        keyfile = get_datadir();
        fprintf(stderr, "Generating .secrets file in %s ...\n", keyfile);
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
