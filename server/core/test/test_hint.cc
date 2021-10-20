/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-10-11
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
#include <cstdlib>
#include <cstring>

#include <maxbase/assert.h>
#include <maxscale/hint.hh>

/**
 * test1    Allocate table of users and mess around with it
 *
 */
static int test1()
{
    HINT* hint;

    /* Hint tests */
    fprintf(stderr,
            "testhint : Add a parameter hint to a null list");
    hint = hint_create_parameter(nullptr, "name", "value");
    mxb_assert_message(NULL != hint, "New hint list should not be null");
    mxb_assert_message(0 == strcmp("value", hint->value.c_str()), "Hint value should be correct");
    mxb_assert_message(0 != hint_exists(&hint, HINT_PARAMETER), "Hint of parameter type should exist");
    fprintf(stderr, "\t..done\nFree hints.");
    if (NULL != hint)
    {
        hint_free(hint);
    }
    fprintf(stderr, "\t..done\n");

    return 0;
}

int main(int argc, char** argv)
{
    int result = 0;

    result += test1();

    exit(result);
}
