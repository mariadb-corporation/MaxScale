/*
 * This file is distributed as part of the MariaDB Corporation MaxScale.  It is free
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
 * Copyright MariaDB Corporation Ab 2013-2014
 */

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <skygw_utils.h>
#include <log_manager.h>

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
        fprintf(stderr,"Message size too small or large, must be at least 1 byte long and "
                "must not exceed 1024 bytes.");
        return 1;
    }

    if (getcwd(cwd, sizeof(cwd)) == NULL ||
        (message = (char*)malloc(sizeof(char) * block_size)) == NULL)
    {
        fprintf(stderr,"Fatal Error, exiting...");
        return 1;
    }

    memset(tmp, 0, 1024);

    sprintf(tmp, "%s", cwd);

    iterations = atoi(argv[1]);
    interval = atoi(argv[2]);

    succp = mxs_log_init(NULL, tmp, MXS_LOG_TARGET_FS);

    if (!succp)
    {
        fprintf(stderr,"Error, log manager initialization failed.\n");
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
            fprintf(stderr,"Error: Message too long");
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
            fprintf(stderr,"Error: log_manager returned %d",err);
            break;
        }
        ts1.tv_nsec = 100 * 1000000;
        nanosleep(&ts1, NULL);
    }

    mxs_log_flush();
    mxs_log_finish();
    free(message);
    return 0;
}
