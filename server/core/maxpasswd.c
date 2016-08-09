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

    mxs_log_set_priority_enabled(LOG_NOTICE, false);
    mxs_log_set_priority_enabled(LOG_INFO, false);
    mxs_log_set_priority_enabled(LOG_DEBUG, false);

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
