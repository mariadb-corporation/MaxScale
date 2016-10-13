/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl.
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
 * 13-10-2014   Martin Brampton     Initial implementation
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

#include <maxscale/gwbitmask.h>

#include <maxscale/skygw_debug.h>

/**
 * test1    Create a bitmap and mess around with it
 *
 */
static int
test1()
{
    static GWBITMASK    bitmask, another;
    int     i;

    /* Hint tests */
    ss_dfprintf(stderr,
                "testgwbitmask : Initialise a bitmask");
    bitmask_init(&bitmask);
    for (i = 0; i < MXS_BITMASK_LENGTH; i++)
    {
        ss_info_dassert(0 == bitmask_isset(&bitmask, i), "All bits should initially be zero");
    }
    ss_info_dassert(0 != bitmask_isallclear(&bitmask), "Should be all clear");
    ss_dfprintf(stderr, "\t..done\nSet an arbitrary bit.");
    bitmask_set(&bitmask, 17);
    bitmask_copy(&another, &bitmask);
    ss_info_dassert(0 != bitmask_isset(&another, 17), "Test bit should be set");
    ss_dfprintf(stderr, "\t..done\nClear the arbitrary bit.");
    bitmask_clear(&bitmask, 17);
    ss_info_dassert(0 != bitmask_isallclear(&bitmask), "Should be all clear");
    ss_info_dassert(0 == bitmask_isset(&bitmask, 17), "Test bit should be clear");

    ss_dfprintf(stderr, "\t..done\nFree the bitmask.");
    bitmask_free(&bitmask);
    ss_dfprintf(stderr, "\t..done\n");

    return 0;

}

int main(int argc, char **argv)
{
    int result = 0;

    result += test1();

    exit(result);
}

