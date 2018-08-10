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

#include <maxscale/cdefs.h>

#include <stdio.h>
#include <errno.h>
#include <sys/stat.h>
#include <getopt.h>

#include <maxscale/paths.h>
#include <maxscale/log_manager.h>
#include <maxscale/random_jkiss.h>
#include <maxscale/alloc.h>

#include "internal/secrets.h"

#ifdef HAVE_GLIBC
struct option options[] =
{
    {
        "help",
        no_argument,
        NULL,
        'h'
    },
    { NULL, 0, NULL, 0 }
};
#endif

void print_usage(const char* executable, const char* directory)
{
    printf("usage: %s [-h|--help] [path] password\n"
           "\n"
           "This utility creates an encrypted password using a .secrets file\n"
           "that has earlier been created using maxkeys.\n"
           "\n"
           "-h, --help: Display this help.\n"
           "\n"
           "path : The directory where the .secrets file is located, or the path\n"
           "       of the .secrets file itself. Note that the name of the file\n"
           "       must be .secrets.\n"
           "\n"
           "If a path is not provided, the .secrets file is looked for in the\n"
           "directory %s.\n",
           executable, directory);
}

bool path_is_ok(const char* path)
{
    // TODO: The check here is superfluous, in the sense that encryptPassword
    // TODO: checks the same. However, so as not to get annoying notice output
    // TODO: on success, notices are turned off and the absence of the file is
    // TODO: reported as a notice. Thus, without this check maxpasswd would
    // TODO: simply fail, without telling the cause.

    bool rv = false;

    struct stat st;

    if (stat(path, &st) == 0)
    {
        if (S_ISDIR(st.st_mode))
        {
            const char TAIL[] = "/.secrets";

            char file[strlen(path) + sizeof(TAIL)];
            strcpy(file, path);
            strcat(file, TAIL);

            rv = path_is_ok(file);
        }
        else
        {
            // encryptPassword reports of any errors.
            rv = true;
        }
    }
    else
    {
        fprintf(stderr, "error: Could not access %s: %s.\n", path, strerror(errno));
    }

    return rv;
}

int main(int argc, char **argv)
{
    const char* path = get_datadir();

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
            print_usage(argv[0], path);
            exit(EXIT_SUCCESS);
            break;

        default:
            print_usage(argv[0], path);
            exit(EXIT_FAILURE);
            break;
        }
    }

    char* password;

    switch (argc - optind)
    {
    case 2:
        // Two args provided.
        path = argv[optind];
        if (!path_is_ok(path))
        {
            exit(EXIT_FAILURE);
        }
        password = argv[optind + 1];
        break;

    case 1:
        // One arg provided.
        password = argv[optind];
        break;

    default:
        print_usage(argv[0], path);
        exit(EXIT_FAILURE);
        break;
    }

    // We'll ignore errors and steam ahead.
    (void)mxs_log_init(NULL, NULL, MXS_LOG_TARGET_DEFAULT);

    mxs_log_set_priority_enabled(LOG_NOTICE, false);
    mxs_log_set_priority_enabled(LOG_INFO, false);
    mxs_log_set_priority_enabled(LOG_DEBUG, false);

    random_jkiss_init();

    size_t len = strlen(password);

    if (len > MXS_PASSWORD_MAXLEN)
    {
        fprintf(stderr,
                "warning: The provided password is %lu characters long. "
                "Only the first %d will be used.\n", len, MXS_PASSWORD_MAXLEN);
    }

    char used_password[MXS_PASSWORD_MAXLEN + 1];
    strncpy(used_password, password, MXS_PASSWORD_MAXLEN);
    used_password[MXS_PASSWORD_MAXLEN] = 0;

    int rval = EXIT_SUCCESS;

    char* enc = encrypt_password(path, used_password);
    if (enc)
    {
        printf("%s\n", enc);
        MXS_FREE(enc);
    }
    else
    {
        fprintf(stderr, "error: Failed to encode the password.\n");
        rval = EXIT_FAILURE;
    }

    mxs_log_finish();

    return rval;
}
