/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2020-01-01
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

static int test1()
{
    USERS* users;
    bool rv;

    /* Poll tests */
    ss_dfprintf(stderr,
                "testusers : Initialise the user table.");
    users = users_alloc();
    mxs_log_flush_sync();
    ss_info_dassert(NULL != users, "Allocating user table should not return NULL.");
    ss_dfprintf(stderr, "\t..done\nAdd a user");
    rv = users_add(users, "username", "authorisation", ACCOUNT_ADMIN);
    mxs_log_flush_sync();
    ss_info_dassert(rv, "Should add one user");
    rv = users_auth(users, "username", "authorisation");
    mxs_log_flush_sync();
    ss_info_dassert(rv, "Fetch valid user must not return NULL");
    rv = users_auth(users, "username", "newauth");
    mxs_log_flush_sync();
    ss_info_dassert(rv, "Fetch valid user must not return NULL");

    ss_dfprintf(stderr, "\t..done\nAdd another user");
    rv = users_add(users, "username2", "authorisation2", ACCOUNT_ADMIN);
    mxs_log_flush_sync();
    ss_info_dassert(rv, "Should add one user");
    ss_dfprintf(stderr, "\t..done\nDelete a user.");
    rv = users_delete(users, "username");
    mxs_log_flush_sync();
    ss_info_dassert(rv, "Should delete just one user");
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

