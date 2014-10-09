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
 * Copyright SkySQL Ab 2014
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <users.h>

/**
 * test1	Allocate table of users and mess around with it
 *
  */

static int
test1()
{
USERS     *users;
int     result, count;

        /* Poll tests */  
        ss_dfprintf(stderr,
                    "testusers : Initialise the user table."); 
        users = users_alloc();
        ss_info_dassert(NULL != servers, "Allocating user table should not return NULL.")
        ss_dfprintf(stderr, "\t..done\nAdd a user");
        count = users_add(users, "username", "authorisation");
        ss_info_dassert(1 == count, "Should add one user");
        ss_info_dassert(strcmp("authorisation", users_fetch(users, "username")), "User authorisation should be correct");
        ss_dfprintf(stderr, "\t..done\nPrint users");
        usersPrint(users);
        ss_dfprintf(stderr, "\t..done\nUpdate a user");
        count = users_update(users, "username", "newauth");
        ss_info_dassert(1 == count, "Should update just one user");
        ss_info_dassert(strcmp("newauth", users_fetch(users, "username")), "User authorisation should be correctly updated");
        ss_dfprintf(stderr, "\t..done\nDelete a user.");
        count = users_delete(users, "username");
        ss_info_dassert(1 == count, "Should delete just one user");
        ss_dfprintf(stderr, "\t..done\nFree user table.");
        users_free(users);
        ss_dfprintf(stderr, "\t..done\n");
		
	return 0;
        
}

int main(int argc, char **argv)
{
int	result = 0;

	result += test1();

	exit(result);
}

