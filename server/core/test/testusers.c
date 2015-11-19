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

#include <users.h>

#include "log_manager.h"

/**
 * test1	Allocate table of users and mess around with it
 *
  */

static int
test1()
{
USERS   *users;
char    *authdata;
int     result, count;

        /* Poll tests */  
        ss_dfprintf(stderr,
                    "testusers : Initialise the user table."); 
        users = users_alloc();
        mxs_log_flush_sync();
        ss_info_dassert(NULL != users, "Allocating user table should not return NULL.");
        ss_dfprintf(stderr, "\t..done\nAdd a user");
        count = users_add(users, "username", "authorisation");
        mxs_log_flush_sync();
        ss_info_dassert(1 == count, "Should add one user");
        authdata = users_fetch(users, "username");
        mxs_log_flush_sync();
        ss_info_dassert(NULL != authdata, "Fetch valid user must not return NULL");
        ss_info_dassert(0 == strcmp("authorisation", authdata), "User authorisation should be correct");
        ss_dfprintf(stderr, "\t..done\nPrint users");
        usersPrint(users);
        mxs_log_flush_sync();
        ss_dfprintf(stderr, "\t..done\nUpdate a user");
        count = users_update(users, "username", "newauth");
        mxs_log_flush_sync();
        ss_info_dassert(1 == count, "Should update just one user");
        authdata = users_fetch(users, "username");
        mxs_log_flush_sync();
        ss_info_dassert(NULL != authdata, "Fetch valid user must not return NULL");
        ss_info_dassert(0 == strcmp("newauth", authdata), "User authorisation should be correctly updated");

        ss_dfprintf(stderr, "\t..done\nAdd another user");
        count = users_add(users, "username2", "authorisation2");
        mxs_log_flush_sync();
        ss_info_dassert(1 == count, "Should add one user");        
        ss_dfprintf(stderr, "\t..done\nDelete a user.");
        count = users_delete(users, "username");
        mxs_log_flush_sync();
        ss_info_dassert(1 == count, "Should delete just one user");
        ss_dfprintf(stderr, "\t..done\nFree user table.");
        users_free(users);
        mxs_log_flush_sync();
        ss_dfprintf(stderr, "\t..done\n");
		
	return 0;
        
}

int main(int argc, char **argv)
{
int	result = 0;

	result += test1();

	exit(result);
}

