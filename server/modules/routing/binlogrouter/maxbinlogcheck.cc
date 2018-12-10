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
 * @file maxbinlogcheck.c - The MaxScale binlog check utility
 *
 * This utility checks a MySQL 5.6 and MariaDB 10.0.X binlog file and reports
 * any found error or an incomplete transaction.
 * It suggests the pos the file should be trucatetd at.
 */

#include "blr.hh"

#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <maxscale/alloc.h>
#include <maxscale/log.hh>


static void printVersion(const char* progname);
static void printUsage(const char* progname);
static int  set_encryption_options(ROUTER_INSTANCE* inst, char* key_file, char* aes_algo);

#ifdef HAVE_GLIBC
static struct option long_options[] =
{
    {"debug",         no_argument,            0,                      'd'                },
    {"version",       no_argument,            0,                      'V'                },
    {"fix",           no_argument,            0,                      'f'                },
    {"mariadb10",     no_argument,            0,                      'M'                },
    {"header",        no_argument,            0,                      'H'                },
    {"key_file",      required_argument,      0,                      'K'                },
    {"aes_algo",      required_argument,      0,                      'A'                },
    {"replace-event", required_argument,      0,                      'R'                },
    {"remove-trx",    required_argument,      0,                      'T'                },
    {"help",          no_argument,            0,                      '?'                },
    {0,               0,                      0,                      0                  }
};
#endif
const char* binlog_check_version = "2.2.1";

int maxscale_uptime()
{
    return 1;
}

int main(int argc, char** argv)
{
    int option_index = 0;
    int debug_out = 0;
    int mariadb10_compat = 0;
    char* key_file = NULL;
    char* aes_algo = NULL;
    int report_header = 0;
    int c;
    BINLOG_FILE_FIX binlog_file = {0, false, false};

#ifdef HAVE_GLIBC
    while ((c = getopt_long(argc, argv, "dVfMHK:A:R:T:?", long_options, &option_index)) >= 0)
#else
    while ((c = getopt(argc, argv, "dVfMHK:A:R:T:?")) >= 0)
#endif
    {
        switch (c)
        {
        case 'd':
            debug_out = 1;
            break;

        case 'H':
            report_header = BLR_REPORT_REP_HEADER;
            break;

        case 'V':
            printVersion(*argv);
            exit(EXIT_SUCCESS);
            break;

        case 'f':
            binlog_file.fix = true;
            break;

        case 'M':
            mariadb10_compat = 1;
            break;

        case 'K':
            key_file = optarg;
            break;

        case 'A':
            aes_algo = optarg;
            break;

        case 'R':
        case 'T':
            binlog_file.pos = atol(optarg);
            binlog_file.replace_trx = (c == 'T') ? true : false;
            break;

        case '?':
            printUsage(*argv);
            exit(optopt ? EXIT_FAILURE : EXIT_SUCCESS);
        }
    }

    int num_args = optind;

    if (argv[num_args] == NULL)
    {
        printf("ERROR: No binlog file was specified.\n");
        exit(EXIT_FAILURE);
    }

    size_t len = strlen(argv[num_args]);
    if (len > PATH_MAX)
    {
        printf("ERROR: The length of the provided path exceeds %d characters.\n", PATH_MAX);
        exit(EXIT_FAILURE);
    }

    char path[PATH_MAX + 1];
    strcpy(path, argv[num_args]);

    char* name = strrchr(path, '/');
    if (name)
    {
        ++name;
        len = strlen(name);
    }
    else
    {
        name = path;
    }

    if ((len == 0) || (len > BINLOG_FNAMELEN))
    {
        printf("ERROR: The length of the binlog filename is 0 or exceeds %d characters.\n",
               BINLOG_FNAMELEN);
        exit(EXIT_FAILURE);
    }

    ROUTER_INSTANCE* inst = (ROUTER_INSTANCE*)MXS_CALLOC(1, sizeof(ROUTER_INSTANCE));
    if (!inst)
    {
        exit(EXIT_FAILURE);
    }

    int fd = open(path, binlog_file.fix ? O_RDWR : O_RDONLY, 0660);
    if (fd == -1)
    {
        printf("ERROR: Failed to open binlog file %s: %s.\n",
               path,
               strerror(errno));
        MXS_FREE(inst);
        mxs_log_finish();
        exit(EXIT_FAILURE);
    }

    inst->binlog_fd = fd;
    inst->mariadb10_compat = mariadb10_compat;
    strcpy(inst->binlog_name, name);

    // We ignore potential errors.
    mxs_log_init(NULL, NULL, MXS_LOG_TARGET_DEFAULT);
    atexit(mxs_log_finish);
    mxs_log_set_augmentation(0);
    mxs_log_set_priority_enabled(LOG_DEBUG, debug_out);

    MXS_NOTICE("maxbinlogcheck %s", binlog_check_version);

    unsigned long filelen = 0;
    struct stat statb;
    if (fstat(inst->binlog_fd, &statb) == 0)
    {
        filelen = statb.st_size;
    }

    /* If encryption options are in use check  and use them */
    if (set_encryption_options(inst, key_file, aes_algo))
    {
        MXS_FREE(inst);
        mxs_log_finish();
        exit(EXIT_FAILURE);
    }

    MXS_NOTICE("Checking %s (%s), size %lu bytes", path, inst->binlog_name, filelen);

    /* Look first for a transaction that has an event at pos binlog_file.pos */
    if (binlog_file.fix && binlog_file.pos && binlog_file.replace_trx)
    {
        /* Don't modify anything */
        binlog_file.fix = false;

        /* The routine call overwrites binlog_file.pos with transaction BEGIN pos */
        blr_read_events_all_events(inst, &binlog_file, BLR_CHECK_ONLY);

        binlog_file.fix = true;
    }

    /* Now read/check/fix the binary log */
    int ret = blr_read_events_all_events(inst, &binlog_file, debug_out | report_header);

    MXS_NOTICE("Check retcode: %i, Binlog Pos = %lu", ret, inst->binlog_position);

    close(inst->binlog_fd);
    MXS_FREE(inst);

    mxs_log_finish();

    return ret;
}

/**
 * Print version information
 */
static void printVersion(const char* progname)
{
    printf("%s Version %s\n", progname, binlog_check_version);
}

/**
 * Display the --help text.
 */
static void printUsage(const char* progname)
{
    printVersion(progname);

    printf("The MaxScale binlog check utility.\n\n");
    printf("Usage: %s [-f] [-M] [-d] [-V] [-H] [-K file] [-A algo] [-R pos] [-T pos] [<binlog file>]\n\n",
           progname);
    printf("  -f|--fix              Fix binlog file, require write permissions (truncate)\n");
    printf("  -d|--debug            Print debug messages\n");
    printf("  -M|--mariadb10        MariaDB 10 binlog compatibility\n");
    printf("  -V|--version          Print version information and exit\n");
    printf("  -K|--key_file         AES Key file for MariaDB 10.1 binlog file decryption\n");
    printf(
        "  -A|--aes_algo         AES Algorithm for MariaDB 10.1 binlog file decryption (default=AES_CBC, AES_CTR)\n");
    printf("  -H|--header           Print content of binlog event header\n");
    printf("  -R|--replace-event    Replace the event at pos with an IGNORABLE event\n");
    printf(
        "  -T|--remove-trx       Replace all events in the transaction the specified pos belongs to, with IGNORABLE events\n");
    printf("  -?|--help             Print this help text\n");
}

/**
 * Check and set the encryption options
 *
 * @param inst        The current binlog instance
 * @param key_file    The AES Key filename
 * @param aes_algo    The AES algorithm
 * @return            1 on failure, 0 on success
 */
static int set_encryption_options(ROUTER_INSTANCE* inst, char* key_file, char* aes_algo)
{
    if (aes_algo && !key_file)
    {
        MXS_ERROR("AES algorithm set but no KEY file specified, exiting.");
        return 1;
    }

    /* Get the encryption KEY */
    if (key_file)
    {
        inst->encryption.key_management_filename = key_file;
        if (!blr_get_encryption_key(inst))
        {
            return 1;
        }
        else
        {
            /* Check aes algorithm */
            if (aes_algo)
            {
                int ret = blr_check_encryption_algorithm(aes_algo);
                if (ret > -1)
                {
                    inst->encryption.encryption_algorithm = ret;
                }
                else
                {
                    MXS_ERROR("Invalid encryption_algorithm '%s'. "
                              "Supported algorithms: %s",
                              aes_algo,
                              blr_encryption_algorithm_list());
                    return 1;
                }
            }
            else
            {
                inst->encryption.encryption_algorithm = BINLOG_DEFAULT_ENC_ALGO;
            }

            MXS_NOTICE("Decrypting binlog file with algorithm: %s,"
                       " KEY len %lu bits",
                       blr_get_encryption_algorithm(inst->encryption.encryption_algorithm),
                       8 * inst->encryption.key_len);

            return 0;
        }
    }
    else
    {
        return 0;
    }
}
