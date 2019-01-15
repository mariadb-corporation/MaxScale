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
 * @file maxavrocheck.c - Simple Avro file validator
 */

#include <maxavro.hh>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>
#include <limits.h>
#include <getopt.h>

#include <maxscale/log.hh>

static int verbose = 0;
static uint64_t seekto = 0;
static int64_t num_rows = -1;
static bool dump = false;

int check_file(const char* filename)
{
    MAXAVRO_FILE* file = maxavro_file_open(filename);

    if (!file)
    {
        return 1;
    }

    int rval = 0;

    if (!dump)
    {
        printf("File sync marker: ");
        for (size_t i = 0; i < sizeof(file->sync); i++)
        {
            printf("%hhx", file->sync[i]);
        }
        printf("\n");
    }

    /** After the header come the data blocks. Each data block has the number of records
     * in this block and the size of the compressed block encoded as Avro long values
     * followed by the actual data. Each data block ends with an identical, 16 byte sync marker
     * which can be checked to make sure the file is not corrupted. */
    do
    {
        if (seekto > 0)
        {
            maxavro_record_seek(file, seekto);
            seekto = 0;
        }

        if (verbose > 1 || dump)
        {
            json_t* row;
            while (num_rows != 0 && (row = maxavro_record_read_json(file)))
            {
                char* json = json_dumps(row, JSON_PRESERVE_ORDER);
                if (json)
                {
                    printf("%s\n", json);
                    json_decref(row);
                    if (num_rows > 0)
                    {
                        num_rows--;
                    }
                }
                else
                {
                    printf("Failed to read JSON value.\n");
                    return 1;
                }
            }
        }

        if (verbose && !dump)
        {
            printf("Block %lu: %lu records, %lu bytes\n",
                   file->blocks_read,
                   file->records_in_block,
                   file->buffer_size);
        }
    }
    while (num_rows != 0 && maxavro_next_block(file));

    if (maxavro_get_error(file) != MAXAVRO_ERR_NONE)
    {
        printf("Failed to read next data block after data block %lu. "
               "Read %lu records and %lu bytes before failure.\n",
               file->blocks_read,
               file->records_read,
               file->bytes_read);
        rval = 1;
    }
    else if (!dump)
    {
        printf("%s: %lu blocks, %lu records and %lu bytes\n",
               filename,
               file->blocks_read,
               file->records_read,
               file->bytes_read);
    }


    maxavro_file_close(file);
    return rval;
}

static struct option long_options[] =
{
    {"verbose", no_argument, 0, 'v'},
    {"dump",    no_argument, 0, 'd'},
    {"from",    no_argument, 0, 'f'},
    {"count",   no_argument, 0, 'c'},
    {0,         0,           0, 0  }
};

int main(int argc, char** argv)
{

    if (argc < 2)
    {
        printf("Usage: %s FILE\n", argv[0]);
        return 1;
    }

    if (!mxs_log_init(NULL, NULL, MXS_LOG_TARGET_STDOUT))
    {
        fprintf(stderr, "Failed to initialize log.\n");
        return 2;
    }

    char c;
    int option_index;

    while ((c = getopt_long(argc, argv, "vdf:c:", long_options, &option_index)) >= 0)
    {
        switch (c)
        {
        case 'v':
            verbose++;
            break;

        case 'd':
            dump = true;
            break;

        case 'f':
            seekto = strtol(optarg, NULL, 10);
            break;

        case 'c':
            num_rows = strtol(optarg, NULL, 10);
            break;
        }
    }

    int rval = 0;
    char pathbuf[PATH_MAX + 1];
    for (int i = optind; i < argc; i++)
    {
        if (check_file(realpath(argv[i], pathbuf)))
        {
            fprintf(stderr, "Failed to process file: %s\n", argv[i]);
            rval = 1;
        }
    }
    return rval;
}
