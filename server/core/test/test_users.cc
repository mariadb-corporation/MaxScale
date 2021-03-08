/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-03-08
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

// To ensure that ss_info_assert asserts also when builing in non-debug mode.
#if !defined (SS_DEBUG)
#define SS_DEBUG
#endif
#if defined (NDEBUG)
#undef NDEBUG
#endif
#include <cstdio>
#include <cstring>
#include <maxscale/users.hh>
#include "test_utils.hh"

using mxs::USER_ACCOUNT_ADMIN;

static void test1()
{
    mxs::Users users;

    /* Poll tests */
    fprintf(stderr, "Add a user");
    bool rv = users.add("username", "authorisation", USER_ACCOUNT_ADMIN);
    mxb_assert_message(rv, "Should add one user");
    rv = users.authenticate("username", "authorisation");
    mxb_assert_message(rv, "Fetch valid user must not return NULL");
    rv = users.authenticate("username", "newauth");
    mxb_assert_message(!rv, "Fetch invalid user must return NULL");

    fprintf(stderr, "\t..done\nAdd another user");
    rv = users.add("username2", "authorisation2", USER_ACCOUNT_ADMIN);
    mxb_assert_message(rv, "Should add one user");
    fprintf(stderr, "\t..done\nDelete a user.");
    rv = users.remove("username");
    mxb_assert_message(rv, "Should delete just one user");

    fprintf(stderr, "\t..done\nDump users table.");
    json_t* dump = users.to_json();
    mxb_assert_message(dump, "Users should be dumped");
    mxs::Users loaded_users;
    loaded_users.load_json(dump);
    mxb_assert_message(dump, "Users should be loaded");
    json_decref(dump);
    rv = loaded_users.authenticate("username2", "authorisation2");
    mxb_assert_message(rv, "Loaded users should contain users");

    fprintf(stderr, "\t..done\n");
}

int main(int argc, char** argv)
{
    run_unit_test(test1);
    return 0;
}
