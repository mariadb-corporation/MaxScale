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

// To ensure that ss_info_assert asserts also when building in non-debug mode.
#if !defined (SS_DEBUG)
#define SS_DEBUG
#endif
#if defined (NDEBUG)
#undef NDEBUG
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <maxscale/paths.h>
#include <maxscale/adminusers.h>
#include <maxscale/alloc.h>
#include <maxscale/utils.h>
#include <maxscale/users.h>

/**
 * test1    default user
 *
 * Test that the username password admin/mariadb is accepted if no users
 * have been created and that no other users are accepted
 *
 * WARNING: The passwd file must be removed before this test is run
 */
static int test1()
{
    if (admin_verify_inet_user("admin", "mariadb") == 0)
    {
        fprintf(stderr, "admin_verify: test 1.1 (default user) failed.\n");
        return 1;
    }
    if (admin_verify_inet_user("bad", "user"))
    {
        fprintf(stderr, "admin_verify: test 1.2 (wrong user) failed.\n");
        return 1;
    }

    return 0;
}

/**
 * test2    creating users
 *
 * Create a user
 * Try to create a duplicate user - expects a failure
 * Remove that user - expected to succeed as no user need to remain.
 */
static int test2()
{
    const char* err;

    if ((err = admin_enable_linux_account("user0", USER_ACCOUNT_ADMIN)) != NULL)
    {
        fprintf(stderr, "admin_add_user: test 2.1 (add user) failed, %s.\n", err);

        return 1;
    }
    if (admin_enable_linux_account("user0", USER_ACCOUNT_ADMIN) == NULL)
    {
        fprintf(stderr, "admin_add_user: test 2.2 (add user) failed, duplicate.\n");

        return 1;
    }

    /* Deleting the last user is not forbidden so we expect this to succeed */
    if ((err = admin_disable_linux_account("user0")) != NULL)
    {
        fprintf(stderr, "admin_remove_user: test 2.3 (add user) failed, %s.\n", err);

        return 1;
    }

    /* Add the user back, for test5. */
    if ((err = admin_enable_linux_account("user0", USER_ACCOUNT_ADMIN)) != NULL)
    {
        fprintf(stderr, "admin_add_user: test 2.4 (add user) failed, %s.\n", err);

        return 1;
    }

    return 0;
}

/**
 * test3    search/verify users
 *
 * Create a user
 * Search for that user
 * Search for a non-existant user
 * Remove the user
 * Search for the user that was removed
 */
static int test3()
{
    const char* err;

    if ((err = admin_enable_linux_account("user1", USER_ACCOUNT_ADMIN)) != NULL)
    {
        fprintf(stderr, "admin_add_user: test 3.1 (add user) failed, %s.\n", err);

        return 1;
    }

    if (admin_linux_account_enabled("user1") == 0)
    {
        fprintf(stderr, "admin_search_user: test 3.2 (search user) failed.\n");

        return 1;
    }

    if (admin_linux_account_enabled("user2") != 0)
    {
        fprintf(stderr, "admin_search_user: test 3.3 (search user) failed, unexpeted user found.\n");

        return 1;
    }

    if ((err = admin_disable_linux_account("user1")) != NULL)
    {
        fprintf(stderr, "admin_remove_user: test 3.4 (add user) failed, %s.\n", err);

        return 1;
    }

    if (admin_linux_account_enabled("user1"))
    {
        fprintf(stderr, "admin_search_user: test 3.5 (search user) failed - user was deleted.\n");

        return 1;
    }

    return 0;
}

/**
 * test4    verify users
 *
 * Create a numebr of users
 * search for each user in turn
 * verify each user in turn (password verification)
 * Verify each user in turn with incorrect password
 * Randomly verify each user
 * Remove each user
 */
static int test4()
{
    const char* err;
    char user[40], passwd[40];
    int i, n_users = 50;

    for (i = 1; i < n_users; i++)
    {
        sprintf(user, "user%d", i);
        if ((err = admin_enable_linux_account(user, USER_ACCOUNT_ADMIN)) != NULL)
        {
            fprintf(stderr, "admin_add_user: test 4.1 (add user) failed, %s.\n", err);

            return 1;
        }
    }

    for (i = 1; i < n_users; i++)
    {
        sprintf(user, "user%d", i);
        if (admin_linux_account_enabled(user) == 0)
        {
            fprintf(stderr, "admin_search_user: test 4.2 (search user) failed.\n");

            return 1;
        }
    }

    for (i = 1; i < n_users; i++)
    {
        sprintf(user, "user%d", i);
        if ((err = admin_disable_linux_account(user)) != NULL)
        {
            fprintf(stderr, "admin_remove_user: test 4.3 (add user) failed, %s.\n", err);

            return 1;
        }
    }

    return 0;
}

/**
 * test5    remove first user
 *
 * Create a user so that user0 may be removed
 * Remove the first user created (user0)
 */
static int test5()
{
    const char* err;

    if ((err = admin_enable_linux_account("user", USER_ACCOUNT_ADMIN)) != NULL)
    {
        fprintf(stderr, "admin_add_user: test 5.1 (add user) failed, %s.\n", err);

        return 1;
    }

    if ((err = admin_disable_linux_account("user0")) != NULL)
    {
        fprintf(stderr, "admin_remove_user: test 5.2 (add user) failed, %s.\n", err);

        return 1;
    }

    return 0;
}

int main(int argc, char** argv)
{
    int result = 0;
    char* home, buf[1024];

    /** Set datadir to /tmp */
    set_datadir(MXS_STRDUP_A("/tmp"));

    /* Unlink any existing password file before running this test */
    sprintf(buf, "%s/maxadmin-users", get_datadir());
    if (!is_valid_posix_path(buf))
    {
        exit(1);
    }

    unlink(buf);

    admin_users_init();
    result += test1();
    result += test2();
    result += test3();
    result += test4();
    result += test5();

    exit(result);
}
