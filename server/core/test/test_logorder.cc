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

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <maxscale/alloc.h>
#include <maxscale/debug.h>
#include <maxscale/log_manager.h>

static void skygw_log_enable(int priority)
{
    mxs_log_set_priority_enabled(priority, true);
}

static void skygw_log_disable(int priority)
{
    mxs_log_set_priority_enabled(priority, false);
}

int main(int argc, char** argv)
{
    int iterations = 0, i, interval = 10;
    int block_size;
    int succp, err = 0;
    char cwd[1024];
    char tmp[2048];
    char *message;
    long msg_index = 1;
    struct timespec ts1;
    ts1.tv_sec = 0;

    memset(cwd, 0, 1024);
    if (argc < 4)
    {
        fprintf(stderr,
                "Log Manager Log Order Test\n"
                "Writes an ascending number into the error log to determine if log writes are in order.\n"
                "Usage:\t   testorder <iterations> <frequency of log flushes> <size of message in bytes>\n");
        return 1;
    }

    block_size = atoi(argv[3]);
    if (block_size < 1 || block_size > 1024)
    {
        fprintf(stderr, "Message size too small or large, must be at least 1 byte long and "
                "must not exceed 1024 bytes.");
        return 1;
    }

    if (getcwd(cwd, sizeof(cwd)) == NULL ||
        (message = (char*)MXS_MALLOC(sizeof(char) * block_size)) == NULL)
    {
        fprintf(stderr, "Fatal Error, exiting...");
        return 1;
    }

    memset(tmp, 0, 1024);

    sprintf(tmp, "%s", cwd);

    iterations = atoi(argv[1]);
    interval = atoi(argv[2]);

    succp = mxs_log_init(NULL, tmp, MXS_LOG_TARGET_FS);

    if (!succp)
    {
        fprintf(stderr, "Error, log manager initialization failed.\n");
    }
    ss_dassert(succp);

    skygw_log_disable(LOG_INFO);
    skygw_log_disable(LOG_NOTICE);
    skygw_log_disable(LOG_DEBUG);

    for (i = 0; i < iterations; i++)
    {
        sprintf(message, "message|%ld", msg_index++);
        int msgsize = block_size - strlen(message);
        if (msgsize < 0 || msgsize > 8192)
        {
            fprintf(stderr, "Error: Message too long");
            break;
        }
        memset(message + strlen(message), ' ', msgsize);
        memset(message + block_size - 1, '\0', 1);
        if (interval > 0 && i % interval == 0)
        {
            err = MXS_ERROR("%s", message);
        }
        else
        {
            err = MXS_ERROR("%s", message);
        }
        if (err)
        {
            fprintf(stderr, "Error: log_manager returned %d", err);
            break;
        }
        ts1.tv_nsec = 100 * 1000000;
        nanosleep(&ts1, NULL);
    }

    mxs_log_flush();
    mxs_log_finish();
    MXS_FREE(message);
    return 0;
}
