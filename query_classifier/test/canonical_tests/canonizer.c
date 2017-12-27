/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2019-07-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <maxscale/query_classifier.h>
#include <maxscale/buffer.h>
#include <maxscale/paths.h>
#include <maxscale/utils.h>

int main(int argc, char** argv)
{
    unsigned int psize;
    GWBUF* qbuff;
    char *tok;
    char readbuff[4092];
    FILE* infile;
    FILE* outfile;

    if (argc != 3)
    {
        printf("Usage: canonizer <input file> <output file>\n");
        return 1;
    }

    if (!utils_init())
    {
        printf("Utils library init failed.\n");
        return 1;
    }

    set_libdir(strdup("../../qc_sqlite/"));
    set_datadir(strdup("/tmp"));
    set_langdir(strdup("."));
    set_process_datadir(strdup("/tmp"));

    qc_setup("qc_sqlite", NULL);
    qc_process_init(QC_INIT_BOTH);
    qc_thread_init(QC_INIT_BOTH);

    infile = fopen(argv[1], "rb");
    outfile = fopen(argv[2], "wb");

    if (infile == NULL || outfile == NULL)
    {
        printf("Opening files failed.\n");
        return 1;
    }

    while (!feof(infile) && fgets(readbuff, 4092, infile))
    {
        char* nl = strchr(readbuff, '\n');
        if (nl)
        {
            *nl = '\0';
        }
        if (strlen(readbuff) > 0)
        {
            psize = strlen(readbuff) + 1;
            qbuff = gwbuf_alloc(psize + 4);
            *(qbuff->sbuf->data + 0) = (unsigned char) psize;
            *(qbuff->sbuf->data + 1) = (unsigned char) (psize >> 8);
            *(qbuff->sbuf->data + 2) = (unsigned char) (psize >> 16);
            *(qbuff->sbuf->data + 3) = 0x00;
            *(qbuff->sbuf->data + 4) = 0x03;
            memcpy(qbuff->start + 5, readbuff, psize - 1);
            tok = qc_get_canonical(qbuff);
            fprintf(outfile, "%s\n", tok);
            free(tok);
            gwbuf_free(qbuff);
        }
    }
    fclose(infile);
    fclose(outfile);
    qc_process_end(QC_INIT_BOTH);
    return 0;
}
