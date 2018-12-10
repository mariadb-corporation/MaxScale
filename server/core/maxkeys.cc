/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2022-01-01
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
#include "internal/secrets.h"

#include <getopt.h>
#include <stdio.h>

#include <maxscale/log.hh>
#include <maxscale/paths.h>
#include <maxscale/random.h>

#ifdef HAVE_GLIBC
struct option options[] =
{
    {
        "help",
        no_argument,
        NULL,
        'h'
    },
    {NULL, 0, NULL, 0}
};
#endif

void print_usage(const char* executable, const char* directory)
{
    printf("usage: %s [-h|--help] [directory]\n"
           "\n"
           "This utility writes into the file .secrets, in the specified directory, the\n"
           "AES encryption key and init vector that are used by the utility maxpasswd,\n"
           "when encrypting passwords used in the MariaDB MaxScale configuration file.\n"
           "\n"
           "Note that re-creating the .secrets file will invalidate all existing\n"
           "passwords used in the configuration file.\n"
           "\n"
           " -h, --help: Display this help.\n"
           "\n"
           "directory  : The directory where the .secrets file should be created.\n"
           "\n"
           "If a specific directory is not provided, the file is created in\n"
           "%s.\n",
           executable,
           directory);
}

int main(int argc, char** argv)
{
    const char* directory = get_datadir();

    int c;
#ifdef HAVE_GLIBC
    while ((c = getopt_long(argc, argv, "h", options, NULL)) != -1)
#else
    while ((c = getopt(argc, argv, "h")) != -1)
#endif
    {
        switch (c)
        {
        case 'h':
            print_usage(argv[0], directory);
            exit(EXIT_SUCCESS);
            break;

        default:
            print_usage(argv[0], directory);
            exit(EXIT_FAILURE);
            break;
        }
    }

    int rval = EXIT_SUCCESS;

    if (optind == argc)
    {
        fprintf(stderr, "Generating .secrets file in %s.\n", directory);
    }
    else
    {
        directory = argv[optind];
    }

    mxs_log_init(NULL, NULL, MXS_LOG_TARGET_DEFAULT);

    if (secrets_write_keys(directory) != 0)
    {
        fprintf(stderr, "Failed to create the .secrets file.\n");
        rval = EXIT_FAILURE;
    }

    mxs_log_finish();

    return rval;
}
