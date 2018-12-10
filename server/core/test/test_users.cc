/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2022-01-01
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
#if !defined (SS_DEBUG)
#define SS_DEBUG
#endif
#if defined (NDEBUG)
#undef NDEBUG
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <maxscale/users.h>

static int test1()
{
    USERS* users;
    bool rv;

    /* Poll tests */
    fprintf(stderr,
            "testusers : Initialise the user table.");
    users = users_alloc();
    mxb_assert_message(NULL != users, "Allocating user table should not return NULL.");
    fprintf(stderr, "\t..done\nAdd a user");
    rv = users_add(users, "username", "authorisation", USER_ACCOUNT_ADMIN);
    mxb_assert_message(rv, "Should add one user");
    rv = users_auth(users, "username", "authorisation");
    mxb_assert_message(rv, "Fetch valid user must not return NULL");
    rv = users_auth(users, "username", "newauth");
    mxb_assert_message(rv == 0, "Fetch invalid user must return NULL");

    fprintf(stderr, "\t..done\nAdd another user");
    rv = users_add(users, "username2", "authorisation2", USER_ACCOUNT_ADMIN);
    mxb_assert_message(rv, "Should add one user");
    fprintf(stderr, "\t..done\nDelete a user.");
    rv = users_delete(users, "username");
    mxb_assert_message(rv, "Should delete just one user");

    fprintf(stderr, "\t..done\nDump users table.");
    json_t* dump = users_to_json(users);
    mxb_assert_message(dump, "Users should be dumped");
    USERS* loaded_users = users_from_json(dump);
    mxb_assert_message(dump, "Users should be loaded");
    json_decref(dump);
    rv = users_auth(loaded_users, "username2", "authorisation2");
    users_free(loaded_users);
    mxb_assert_message(rv, "Loaded users should contain users");

    fprintf(stderr, "\t..done\nFree user table.");
    users_free(users);
    fprintf(stderr, "\t..done\n");

    return 0;
}

int main(int argc, char** argv)
{
    int result = 0;

    result += test1();

    exit(result);
}
