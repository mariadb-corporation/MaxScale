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
 * 08-09-2014	Martin Brampton		Initial implementation
 *
 * @endverbatim
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <maxscale_test.h>
#include <service.h>
#include <poll.h>
/**
 * test1	Allocate a service and do lots of other things
 *
  */
static int
test1()
{
SERVICE   *service;
int     result;
int argc = 3;
char buffer[1024];
sprintf(buffer,"%s",TEST_LOG_DIR);
char* argv[] = {
  "log_manager",
  "-j",
  buffer,
  NULL
};
skygw_logmanager_init(argc,argv);
poll_init();
        /* Service tests */
        ss_dfprintf(stderr,
                    "testservice : creating service called MyService with router nonexistent"); 
        service = service_alloc("MyService", "non-existent");
        skygw_log_sync_all();
        ss_info_dassert(NULL == service, "New service with invalid router should be null");
        ss_info_dassert(0 == service_isvalid(service), "Service must not be valid after incorrect creation");
        ss_dfprintf(stderr, "\t..done\nValid service creation, router testroute.");
        service = service_alloc("MyService", "testroute");
        skygw_log_sync_all();
        ss_info_dassert(NULL != service, "New service with valid router must not be null");
        ss_info_dassert(0 != service_isvalid(service), "Service must be valid after creation");
        ss_info_dassert(0 == strcmp("MyService", service_get_name(service)), "Service must have given name");
        ss_dfprintf(stderr, "\t..done\nAdding protocol HTTPD.");
        ss_info_dassert(0 != serviceAddProtocol(service, "HTTPD", "localhost", 9876), "Add Protocol should succeed");
        ss_info_dassert(0 != serviceHasProtocol(service, "HTTPD", 9876), "Service should have new protocol as requested");
        serviceStartProtocol(service, "HTTPD", 9876);
        skygw_log_sync_all();
        ss_dfprintf(stderr, "\t..done\nStarting Service.");
        result = serviceStart(service);
        skygw_log_sync_all();
        ss_info_dassert(0 != result, "Start should succeed");
        result = serviceStop(service);
        skygw_log_sync_all();
        ss_info_dassert(0 != result, "Stop should succeed");
        result = serviceStartAll();
        skygw_log_sync_all();
        ss_info_dassert(0 != result, "Start all should succeed");
        
        ss_dfprintf(stderr, "\t..done\nStopping Service.");
        ss_info_dassert(0 != serviceStop(service), "Stop should succeed");
        ss_dfprintf(stderr, "\t..done\nFreeing Service.");
        ss_info_dassert(0 != service_free(service), "Free should succeed");
        ss_dfprintf(stderr, "\t..done\n");
		
	return 0;
        
}

int main(int argc, char **argv)
{
int	result = 0;

	result += test1();

	exit(result);
}
