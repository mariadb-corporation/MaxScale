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

/**
 *
 * @verbatim
 * Revision History
 *
 * Date         Who                     Description
 * 09-03-2015   Markus Mäkelä           Initial implementation
 * 10-03-2015   Massimiliano Pinto      Added http_check
 *
 * @endverbatim
 */

// To ensure that ss_info_assert asserts also when builing in non-debug mode.
#if !defined(SS_DEBUG)
#define SS_DEBUG
#endif
#if defined(NDEBUG)
#undef NDEBUG
#endif
#define FAILTEST(s) printf("TEST FAILED: " s "\n");return 1;
#include <mysql.h>
#include <stdio.h>
#include <maxscale/notification.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <maxscale/alloc.h>
#include <maxscale/housekeeper.h>
#include <maxscale/buffer.h>
#include <regex.h>
#include <maxscale/maxscale_test.h>

#include "../load_utils.c"

static char* server_options[] =
{
    "MariaDB Corporation MaxScale",
    "--no-defaults",
    "--datadir=.",
    "--language=.",
    "--skip-innodb",
    "--default-storage-engine=myisam",
    NULL
};

const int num_elements = (sizeof(server_options) / sizeof(char *)) - 1;

static char* server_groups[] =
{
    "embedded",
    "server",
    "server",
    "embedded",
    "server",
    "server",
    NULL
};

int config_load(char *);
void config_enable_feedback_task(void);
int do_http_post(GWBUF *buffer, void *cfg);

int main(int argc, char** argv)
{
    FEEDBACK_CONF* fc;
    GWBUF* buf;
    regex_t re;
    char* home;
    char* cnf;

    hkinit();

    cnf = MXS_MALLOC(sizeof(char) * (strlen(TEST_DIR) + strlen("/maxscale.cnf") + 1));
    MXS_ABORT_IF_NULL(cnf);
    sprintf(cnf, "%s/maxscale.cnf", TEST_DIR);

    printf("Config: %s\n", cnf);


    if (mysql_library_init(num_elements, server_options, server_groups))
    {
        FAILTEST("Failed to initialize embedded library.");
    }

    config_load(cnf);
    config_enable_feedback_task();
    if ((fc = config_get_feedback_data()) == NULL)
    {
        FAILTEST("Configuration for Feedback was NULL.");
    }


    regcomp(&re, fc->feedback_user_info, 0);

    module_create_feedback_report(&buf, NULL, fc);

    if (regexec(&re, (char*)buf->start, 0, NULL, 0))
    {
        FAILTEST("Regex match of 'user_info' failed.");
    }

    if (do_http_post(buf, fc) != 0)
    {
        FAILTEST("Http send failed\n");
    }
    mysql_library_end();
    return 0;
}
