/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-11-16
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * @file maxkeys.c  - Create the random encryption keys for maxscale
 */
#include <maxscale/ccdefs.hh>
#include <getopt.h>
#include <sys/types.h>
#include <pwd.h>
#include <stdio.h>
#include <unistd.h>
#include <maxscale/paths.h>
#include <maxscale/random.h>
#include "internal/secrets.hh"

#ifdef HAVE_GLIBC
struct option options[] =
{
    {"help", no_argument,       NULL, 'h'},
    {"user", required_argument, NULL, 'u'},
    {NULL,   0,                 NULL, 0  }
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
           " -h, --help    Display this help\n"
           " -u, --user    Sets the owner of the .secrets file (default: maxscale)\n"
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
    std::string directory = get_datadir();
    std::string username = "maxscale";

    int c;
#ifdef HAVE_GLIBC
    while ((c = getopt_long(argc, argv, "hu:", options, NULL)) != -1)
#else
    while ((c = getopt(argc, argv, "hu:")) != -1)
#endif
    {
        switch (c)
        {
        case 'h':
            print_usage(argv[0], directory.c_str());
            exit(EXIT_SUCCESS);
            break;

        case 'u':
            username = optarg;
            break;

        default:
            print_usage(argv[0], directory.c_str());
            exit(EXIT_FAILURE);
            break;
        }
    }

    int rval = EXIT_SUCCESS;

    if (optind == argc)
    {
        fprintf(stderr, "Generating .secrets file in %s.\n", directory.c_str());
    }
    else
    {
        directory = argv[optind];
    }

    mxs_log_init(NULL, NULL, MXS_LOG_TARGET_DEFAULT);

    if (secrets_write_keys(directory.c_str()) == 0)
    {
        std::string filename = directory + "/.secrets";

        if (auto user = getpwnam(username.c_str()))
        {
            if (chown(filename.c_str(), user->pw_uid, user->pw_gid) == -1)
            {
                fprintf(stderr, "Failed to give '%s' ownership of '%s': %d, %s",
                        username.c_str(), filename.c_str(), errno, strerror(errno));
            }
        }
        else
        {
            fprintf(stderr, "Could not find user '%s' when attempting to change ownership of '%s': %d, %s",
                    username.c_str(), filename.c_str(), errno, strerror(errno));
        }
    }
    else
    {
        fprintf(stderr, "Failed to create the .secrets file.\n");
        rval = EXIT_FAILURE;
    }

    mxs_log_finish();

    return rval;
}
