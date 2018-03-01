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
 * Date         Who                 Description
 * 08-10-2014   Martin Brampton     Initial implementation
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

#include <maxscale/users.h>

#include <maxscale/log_manager.h>

/**
 * test1    Allocate table of users and mess around with it
 *
  */

static int
test1()
{
    USERS      *users;
    const char *authdata;
    int        result, count;

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
    int result = 0;

    result += test1();

    exit(result);
}

