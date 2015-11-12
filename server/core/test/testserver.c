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
 * Date		Who			Description
 * 08-10-2014	Martin Brampton		Initial implementation
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <server.h>
#include <log_manager.h>
/**
 * test1	Allocate a server and do lots of other things
 *
  */
static int
test1()
{
SERVER   *server;
int     result;
char    *status;

        /* Server tests */
        ss_dfprintf(stderr,
                    "testserver : creating server called MyServer"); 
        server = server_alloc("MyServer", "HTTPD", 9876);
        mxs_log_flush_sync();

        //ss_info_dassert(NULL != service, "New server with valid protocol and port must not be null");
        //ss_info_dassert(0 != service_isvalid(service), "Service must be valid after creation");

        ss_dfprintf(stderr, "\t..done\nTest Parameter for Server.");
        ss_info_dassert(NULL == serverGetParameter(server, "name"), "Parameter should be null when not set");
        serverAddParameter(server, "name", "value");
        mxs_log_flush_sync();
        ss_info_dassert(0 == strcmp("value", serverGetParameter(server, "name")), "Parameter should be returned correctly");
        ss_dfprintf(stderr, "\t..done\nTesting Unique Name for Server.");
        ss_info_dassert(NULL == server_find_by_unique_name("uniquename"), "Should not find non-existent unique name.");
        server_set_unique_name(server, "uniquename");
        mxs_log_flush_sync();
        ss_info_dassert(server == server_find_by_unique_name("uniquename"), "Should find by unique name.");
        ss_dfprintf(stderr, "\t..done\nTesting Status Setting for Server.");
        status = server_status(server);
        mxs_log_flush_sync();
        ss_info_dassert(0 == strcmp("Running", status), "Status of Server should be Running by default.");
        if (NULL != status) free(status);
        server_set_status(server, SERVER_MASTER);
        status = server_status(server);
        mxs_log_flush_sync();
        ss_info_dassert(0 == strcmp("Master, Running", status), "Should find correct status.");
        server_clear_status(server, SERVER_MASTER);		
		free(status);
        status = server_status(server);
        mxs_log_flush_sync();
        ss_info_dassert(0 == strcmp("Running", status), "Status of Server should be Running after master status cleared.");
        if (NULL != status) free(status);
        ss_dfprintf(stderr, "\t..done\nRun Prints for Server and all Servers.");
        printServer(server);
        printAllServers();
        mxs_log_flush_sync();
        ss_dfprintf(stderr, "\t..done\nFreeing Server.");
        ss_info_dassert(0 != server_free(server), "Free should succeed");
        ss_dfprintf(stderr, "\t..done\n");
	return 0;
        
}

int main(int argc, char **argv)
{
int	result = 0;

	result += test1();

	exit(result);
}
