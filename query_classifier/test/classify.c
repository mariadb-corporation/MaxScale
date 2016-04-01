/*
 * This file is distributed as part of the MariaDB Corporation MaxScale. It is free
 * software: you can redistribute it and/or modify it under the terms of the
 * GNU General Public License as published by the Free Software Foundation,
 * version 2.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Copyright MariaDB Corporation Ab 2013-2016
 *
 */
#include <my_config.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <query_classifier.h>
#include <buffer.h>
#include <mysql.h>
#include <unistd.h>
#include <gwdirs.h>
#include <log_manager.h>

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
            GWBUF* buff = gwbuf_alloc(qlen + 6);
            *((unsigned char*)(buff->start)) = qlen;
            *((unsigned char*)(buff->start + 1)) = (qlen >> 8);
            *((unsigned char*)(buff->start + 2)) = (qlen >> 16);
            *((unsigned char*)(buff->start + 3)) = 0x00;
            *((unsigned char*)(buff->start + 4)) = 0x03;
            memcpy(buff->start + 5, strbuff, qlen);
            memmove(strbuff, tok + 1, strsz - qlen);
            strsz -= qlen;
            memset(strbuff + strsz, 0, buffsz - strsz);
            qc_query_type_t type = qc_get_type(buff);
            char qtypestr[64];
            char expbuff[256];
            int expos = 0;

            while ((rd = fgetc(expected)) != '\n' && !feof(expected))
            {
                expbuff[expos++] = rd;
            }
            expbuff[expos] = '\0';

            if (type == QUERY_TYPE_UNKNOWN)
            {
                sprintf(qtypestr, "QUERY_TYPE_UNKNOWN");
            }
            if (type & QUERY_TYPE_LOCAL_READ)
            {
                sprintf(qtypestr, "QUERY_TYPE_LOCAL_READ");
            }
            if (type & QUERY_TYPE_READ)
            {
                sprintf(qtypestr, "QUERY_TYPE_READ");
            }
            if (type & QUERY_TYPE_WRITE)
            {
                sprintf(qtypestr, "QUERY_TYPE_WRITE");
            }
            if (type & QUERY_TYPE_MASTER_READ)
            {
                sprintf(qtypestr, "QUERY_TYPE_MASTER_READ");
            }
            if (type & QUERY_TYPE_SESSION_WRITE)
            {
                sprintf(qtypestr, "QUERY_TYPE_SESSION_WRITE");
            }
            if (type & QUERY_TYPE_USERVAR_READ)
            {
                sprintf(qtypestr, "QUERY_TYPE_USERVAR_READ");
            }
            if (type & QUERY_TYPE_SYSVAR_READ)
            {
                sprintf(qtypestr, "QUERY_TYPE_SYSVAR_READ");
            }
            if (type & QUERY_TYPE_GSYSVAR_READ)
            {
                sprintf(qtypestr, "QUERY_TYPE_GSYSVAR_READ");
            }
            if (type & QUERY_TYPE_GSYSVAR_WRITE)
            {
                sprintf(qtypestr, "QUERY_TYPE_GSYSVAR_WRITE");
            }
            if (type & QUERY_TYPE_BEGIN_TRX)
            {
                sprintf(qtypestr, "QUERY_TYPE_BEGIN_TRX");
            }
            if (type & QUERY_TYPE_ENABLE_AUTOCOMMIT)
            {
                sprintf(qtypestr, "QUERY_TYPE_ENABLE_AUTOCOMMIT");
            }
            if (type & QUERY_TYPE_DISABLE_AUTOCOMMIT)
            {
                sprintf(qtypestr, "QUERY_TYPE_DISABLE_AUTOCOMMIT");
            }
            if (type & QUERY_TYPE_ROLLBACK)
            {
                sprintf(qtypestr, "QUERY_TYPE_ROLLBACK");
            }
            if (type & QUERY_TYPE_COMMIT)
            {
                sprintf(qtypestr, "QUERY_TYPE_COMMIT");
            }
            if (type & QUERY_TYPE_PREPARE_NAMED_STMT)
            {
                sprintf(qtypestr, "QUERY_TYPE_PREPARE_NAMED_STMT");
            }
            if (type & QUERY_TYPE_PREPARE_STMT)
            {
                sprintf(qtypestr, "QUERY_TYPE_PREPARE_STMT");
            }
            if (type & QUERY_TYPE_EXEC_STMT)
            {
                sprintf(qtypestr, "QUERY_TYPE_EXEC_STMT");
            }
            if (type & QUERY_TYPE_CREATE_TMP_TABLE)
            {
                sprintf(qtypestr, "QUERY_TYPE_CREATE_TMP_TABLE");
            }
            if (type & QUERY_TYPE_READ_TMP_TABLE)
            {
                sprintf(qtypestr, "QUERY_TYPE_READ_TMP_TABLE");
            }

            if (strcmp(qtypestr, expbuff) != 0)
            {
                printf("Error in output: '%s' was expected but got '%s'", expbuff, qtypestr);
                rc = 1;
            }

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

    if (argc == 3)
    {
        set_libdir(strdup("../qc_mysqlembedded/"));
        set_datadir(strdup("/tmp"));
        set_langdir(strdup("."));
        set_process_datadir(strdup("/tmp"));

        if (mxs_log_init(NULL, ".", MXS_LOG_TARGET_DEFAULT))
        {
            if (qc_init("qc_mysqlembedded"))
            {
                rc = run(argv[1], argv[2]);
                qc_end();
            }
            else
            {
                fprintf(stderr, "error: %s: Could not initialize query classifier library.\n", argv[0]);
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
