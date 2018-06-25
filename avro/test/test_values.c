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

#include <maxavro.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>

const char *testfile = "test.db";
const char *testschema = "";

void write_file()
{
    FILE *file = fopen(testfile, "wb");
    fclose(file);
}

int main(int argc, char** argv)
{
    MAXAVRO_FILE *file = maxavro_file_open(testfile);

    if (!file)
    {
        return 1;
    }

    uint64_t blocks = 0;

    while (maxavro_next_block(file))
    {
        blocks++;
    }

    uint64_t blocksread = file->blocks_read;
    maxavro_file_close(file);
    return blocks != blocksread;
}
