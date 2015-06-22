/*
 * This file is distributed as part of MaxScale.  It is free
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
 * Copyright MariaDB Corporation Ab 2014
 */

/**
 *
 * @verbatim
 * Revision History
 *
 * Date         Who                     Description
 * 09-03-2015   Markus Mäkelä         Initial implementation
 * 10-03-2015   Massimiliano Pinto      Added http_check
 *
 * @endverbatim
 */

#define FAILTEST(s) printf("TEST FAILED: " s "\n");return 1;
#include <my_config.h>
#include <mysql.h>
#include <stdio.h>
#include <notification.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <housekeeper.h>
#include <buffer.h>
#include <regex.h>

static char* server_options[] = {
    "MariaDB Corporation MaxScale",
    "--no-defaults",
    "--datadir=.",
    "--language=.",
    "--skip-innodb",
    "--default-storage-engine=myisam",
    NULL
};

const int num_elements = (sizeof(server_options) / sizeof(char *)) - 1;

static char* server_groups[] = {
    "embedded",
    "server",
    "server",
    "embedded",
    "server",
    "server",
    NULL
};


int main(int argc, char** argv)
{
    FEEDBACK_CONF* fc;
    GWBUF* buf;
    regex_t re;
    char* home;
    char* cnf;

    hkinit();

    cnf = strdup("/etc/MaxScale.cnf");

    printf("Config: %s\n",cnf);


       if(mysql_library_init(num_elements, server_options, server_groups))
       {
	   FAILTEST("Failed to initialize embedded library.");
       }

    config_load(cnf);

    if ((fc = config_get_feedback_data()) == NULL)
    {
        FAILTEST("Configuration for Feedback was NULL.");
    }


    regcomp(&re,fc->feedback_user_info,0);

    module_create_feedback_report(&buf,NULL,fc);

    if(regexec(&re,(char*)buf->start,0,NULL,0))
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
