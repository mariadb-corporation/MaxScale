/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-01-25
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
#include <maxscale/paths.hh>
#include <maxbase/alloc.h>
#include <maxscale/utils.h>
#include <maxscale/users.hh>
#include "../internal/adminusers.hh"

using mxs::USER_ACCOUNT_ADMIN;

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

int main(int argc, char** argv)
{
    int result = 0;
    char* home, buf[1024];

    /** Set datadir to /tmp */
    mxs::set_datadir("/tmp");

    /* Unlink any existing password files before running this test */
    sprintf(buf, "%s/maxadmin-users", mxs::datadir());
    if (!is_valid_posix_path(buf))
    {
        exit(1);
    }

    unlink(buf);

    sprintf(buf, "%s/passwd", mxs::datadir());
    if (!is_valid_posix_path(buf))
    {
        exit(1);
    }

    unlink(buf);

    rest_users_init();
    result += test1();

    exit(result);
}
