/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2020-01-01
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
#include <mysql.h>
#include <unistd.h>
#include <maxscale/paths.h>
#include <maxscale/log_manager.h>

char* append(char* types, const char* type_name, size_t* lenp)
{
    size_t len = strlen(type_name) + 1;

    if (types)
    {
        len += 1;
    }

    *lenp += len;

    char* tmp = realloc(types, *lenp);

    if (types)
    {
        types = tmp;
        strcat(types, "|");
        strcat(types, type_name);
    }
    else
    {
        types = tmp;
        strcpy(types, type_name);
    }


    return types;
}

char* get_types_as_string(uint32_t types)
{
    char* s = NULL;
    size_t len = 0;

    if (types & QUERY_TYPE_LOCAL_READ)
    {
        s = append(s, "QUERY_TYPE_LOCAL_READ", &len);
    }
    if (types & QUERY_TYPE_READ)
    {
        s = append(s, "QUERY_TYPE_READ", &len);
    }
    if (types & QUERY_TYPE_WRITE)
    {
        s = append(s, "QUERY_TYPE_WRITE", &len);
    }
    if (types & QUERY_TYPE_MASTER_READ)
    {
        s = append(s, "QUERY_TYPE_MASTER_READ", &len);
    }
    if (types & QUERY_TYPE_SESSION_WRITE)
    {
        s = append(s, "QUERY_TYPE_SESSION_WRITE", &len);
    }
    if (types & QUERY_TYPE_USERVAR_READ)
    {
        s = append(s, "QUERY_TYPE_USERVAR_READ", &len);
    }
    if (types & QUERY_TYPE_SYSVAR_READ)
    {
        s = append(s, "QUERY_TYPE_SYSVAR_READ", &len);
    }
    if (types & QUERY_TYPE_GSYSVAR_READ)
    {
        s = append(s, "QUERY_TYPE_GSYSVAR_READ", &len);
    }
    if (types & QUERY_TYPE_GSYSVAR_WRITE)
    {
        s = append(s, "QUERY_TYPE_GSYSVAR_WRITE", &len);
    }
    if (types & QUERY_TYPE_BEGIN_TRX)
    {
        s = append(s, "QUERY_TYPE_BEGIN_TRX", &len);
    }
    if (types & QUERY_TYPE_ENABLE_AUTOCOMMIT)
    {
        s = append(s, "QUERY_TYPE_ENABLE_AUTOCOMMIT", &len);
    }
    if (types & QUERY_TYPE_DISABLE_AUTOCOMMIT)
    {
        s = append(s, "QUERY_TYPE_DISABLE_AUTOCOMMIT", &len);
    }
    if (types & QUERY_TYPE_ROLLBACK)
    {
        s = append(s, "QUERY_TYPE_ROLLBACK", &len);
    }
    if (types & QUERY_TYPE_COMMIT)
    {
        s = append(s, "QUERY_TYPE_COMMIT", &len);
    }
    if (types & QUERY_TYPE_PREPARE_NAMED_STMT)
    {
        s = append(s, "QUERY_TYPE_PREPARE_NAMED_STMT", &len);
    }
    if (types & QUERY_TYPE_PREPARE_STMT)
    {
        s = append(s, "QUERY_TYPE_PREPARE_STMT", &len);
    }
    if (types & QUERY_TYPE_EXEC_STMT)
    {
        s = append(s, "QUERY_TYPE_EXEC_STMT", &len);
    }
    if (types & QUERY_TYPE_CREATE_TMP_TABLE)
    {
        s = append(s, "QUERY_TYPE_CREATE_TMP_TABLE", &len);
    }
    if (types & QUERY_TYPE_READ_TMP_TABLE)
    {
        s = append(s, "QUERY_TYPE_READ_TMP_TABLE", &len);
    }
    if (types & QUERY_TYPE_SHOW_DATABASES)
    {
        s = append(s, "QUERY_TYPE_SHOW_DATABASES", &len);
    }
    if (types & QUERY_TYPE_SHOW_TABLES)
    {
        s = append(s, "QUERY_TYPE_SHOW_TABLES", &len);
    }
    if (types & QUERY_TYPE_DEALLOC_PREPARE)
    {
        s = append(s, "QUERY_TYPE_DEALLOC_PREPARE", &len);
    }

    if (!s)
    {
        s = append(s, "QUERY_TYPE_UNKNOWN", &len);
    }

    return s;
}

int test(FILE* input, FILE* expected)
{
    int rc = EXIT_SUCCESS;

    int buffsz = getpagesize(), strsz = 0;
    char buffer[1024], *strbuff = (char*)calloc(buffsz, sizeof(char));

    int rd;

    while ((rd = fread(buffer, sizeof(char), 1023, input)))
    {
        /**Fill the read buffer*/

        if (strsz + rd >= buffsz)
        {
            char* tmp = realloc(strbuff, (buffsz * 2) * sizeof(char));

            if (tmp == NULL)
            {
                free(strbuff);
                fprintf(stderr, "Error: Memory allocation failed.");
                return 1;
            }
            strbuff = tmp;
            buffsz *= 2;
        }

        memcpy(strbuff + strsz, buffer, rd);
        strsz += rd;
        *(strbuff + strsz) = '\0';

        char *tok, *nlptr;

        /**Remove newlines*/
        while ((nlptr = strpbrk(strbuff, "\n")) != NULL && (nlptr - strbuff) < strsz)
        {
            memmove(nlptr, nlptr + 1, strsz - (nlptr + 1 - strbuff));
            strsz -= 1;
        }

        /**Parse read buffer for full queries*/

        while (strpbrk(strbuff, ";") != NULL)
        {
            tok = strpbrk(strbuff, ";");
            unsigned int qlen = tok - strbuff + 1;
            unsigned int payload_len = qlen + 1;
            unsigned int buf_len = payload_len + 4;
            GWBUF* buff = gwbuf_alloc(buf_len);
            *((unsigned char*)(GWBUF_DATA(buff))) = payload_len;
            *((unsigned char*)(GWBUF_DATA(buff) + 1)) = (payload_len >> 8);
            *((unsigned char*)(GWBUF_DATA(buff) + 2)) = (payload_len >> 16);
            *((unsigned char*)(GWBUF_DATA(buff) + 3)) = 0x00;
            *((unsigned char*)(GWBUF_DATA(buff) + 4)) = 0x03;
            memcpy(GWBUF_DATA(buff) + 5, strbuff, qlen);
            memmove(strbuff, tok + 1, strsz - qlen);
            strsz -= qlen;
            memset(strbuff + strsz, 0, buffsz - strsz);
            qc_query_type_t type = qc_get_type_mask(buff);
            char expbuff[256];
            int expos = 0;

            while ((rd = fgetc(expected)) != '\n' && !feof(expected))
            {
                expbuff[expos++] = rd;
            }
            expbuff[expos] = '\0';

            char *qtypestr = get_types_as_string(type);
            const char* q = (const char*) GWBUF_DATA(buff) + 5;

            printf("Query   : %.*s\n", qlen, q);
            printf("Reported: %s\n", qtypestr);

            if (strcmp(qtypestr, expbuff) == 0)
            {
                printf("OK\n");
            }
            else
            {
                printf("ERROR   : %s\n", expbuff);
                rc = 1;
            }

            printf("\n");

            free(qtypestr);

            gwbuf_free(buff);
        }
    }

    free(strbuff);
    return rc;
}

int run(const char* input_filename, const char* expected_filename)
{
    int rc = EXIT_FAILURE;

    FILE *input = fopen(input_filename, "rb");

    if (input)
    {
        FILE *expected = fopen(expected_filename, "rb");

        if (expected)
        {
            rc = test(input, expected);
            fclose(expected);
        }
        else
        {
            fprintf(stderr, "error: Failed to open file %s.", expected_filename);
        }

        fclose(input);
    }
    else
    {
        fprintf(stderr, "error: Failed to open file %s", input_filename);
    }

    return rc;
}

int main(int argc, char** argv)
{
    int rc = EXIT_FAILURE;

    if ((argc == 3) || (argc == 4))
    {
        const char* lib;
        char* libdir;
        const char* input_name;
        const char* expected_name;

        if (argc == 3)
        {
            lib = "qc_mysqlembedded";
            libdir = strdup("../qc_mysqlembedded");
            input_name = argv[1];
            expected_name = argv[2];
        }
        else
        {
            lib = argv[1];
            input_name = argv[2];
            expected_name = argv[3];

            size_t sz = strlen(lib);
            char buffer[sz + 3 + 1]; // "../" and terminating NULL.
            sprintf(buffer, "../%s", lib);

            libdir = strdup(buffer);
        }

        set_libdir(libdir);
        set_datadir(strdup("/tmp"));
        set_langdir(strdup("."));
        set_process_datadir(strdup("/tmp"));

        if (mxs_log_init(NULL, ".", MXS_LOG_TARGET_DEFAULT))
        {
            if (qc_setup(lib, QC_SQL_MODE_DEFAULT, NULL) &&
                qc_process_init(QC_INIT_BOTH) &&
                qc_thread_init(QC_INIT_BOTH))
            {
                //  Version encoded as MariaDB encodes the version, i.e.:
                //  version = major * 10000 + minor * 100 + patch
                uint64_t version = 10 * 10000 + 3 * 100;

                qc_set_server_version(version);
                rc = run(input_name, expected_name);
                qc_process_end(QC_INIT_BOTH);
            }
            else
            {
                fprintf(stderr, "error: %s: Could not initialize query classifier library %s.\n",
                        argv[0], lib);
            }

            mxs_log_finish();
        }
        else
        {
            fprintf(stderr, "error: %s: Could not initialize log.\n", argv[0]);
        }
    }
    else
    {
        fprintf(stderr, "Usage: classify <input> <expected output>\n");
    }

    return rc;
}
