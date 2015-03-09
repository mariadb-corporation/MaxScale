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
 * Date			Who				Description
 * 09-03-2015	Markus Mäkelä	Initial implementation
 *
 * @endverbatim
 */

#define FAILTEST(s) printf("TEST FAILED: " s "\n");return 1; 

#include <stdio.h>
#include <notification.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <housekeeper.h>
#include <buffer.h>
#include <regex.h>

int main(int argc, char** argv)
{
    FEEDBACK_CONF* fc;
    char* home;
    char* cnf;
    GWBUF* buf;
    regex_t re;

    hkinit();
    home = getenv("MAXSCALE_HOME");

    if(home == NULL)
    {
        FAILTEST("MAXSCALE_HOME was not defined.");
    }
    printf("Home: %s\n",home);

    cnf = malloc(strlen(home) + strlen("/etc/MaxScale.cnf") + 1);
    strcpy(cnf,home);
    strcat(cnf,"/etc/MaxScale.cnf");

    printf("Config: %s\n",cnf);

    config_load(cnf);

    if((fc = config_get_feedback_data()) == NULL ||
       fc->feedback_user_info == NULL)
    {
        FAILTEST("Configuration was NULL.");
    }

    regcomp(&re,fc->feedback_user_info,0);

    module_create_feedback_report(&buf,NULL,fc);
    printf("%s",(char*)buf->start);

    if(regexec(&re,(char*)buf->start,0,NULL,0))
    {
        FAILTEST("Regex match of 'user_info' failed.");
    }

    return 0;
}
