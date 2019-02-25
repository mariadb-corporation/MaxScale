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
 * 19-08-2014   Mark Riddoch        Initial implementation
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

#include <maxscale/alloc.h>
#include <maxscale/paths.h>

#include "../internal/filter.hh"

namespace
{

MXS_CONFIG_PARAMETER empty;

}
/**
 * test1    Filter creation, finding and deletion
 *
 */
static int test1()
{
    SFilterDef f1, f2;

    if ((f1 = filter_alloc("test1", "qlafilter", &empty)) == NULL)
    {
        fprintf(stderr, "filter_alloc: test 1 failed.\n");
        return 1;
    }
    if ((f2 = filter_find("test1")) == NULL)
    {
        fprintf(stderr, "filter_find: test 2 failed.\n");
        return 1;
    }
    filter_free(f1);
    if ((f2 = filter_find("test1")) != NULL)
    {
        fprintf(stderr, "filter_find: test 3 failed delete.\n");
        return 1;
    }

    return 0;
}


/**
 * Passive tests for filter_add_option and filter_add_parameter
 *
 * These tests add options and parameters to a filter, the only failure
 * is related hard crashes, such as SIGSEGV etc. as there are no good hooks
 * to check the creation of parameters and options currently.
 */
static int test2()
{
    SFilterDef f1;

    if ((f1 = filter_alloc("test1", "qlafilter", &empty)) == NULL)
    {
        fprintf(stderr, "filter_alloc: test 1 failed.\n");
        return 1;
    }

    filter_free(f1);
    return 0;
}


/**
 * test3    Filter creation, finding and deletion soak test
 *
 */
static int test3()
{
    SFilterDef f1;
    char name[40];
    int i, n_filters = 1000;

    for (i = 0; i < n_filters; i++)
    {
        sprintf(name, "filter%d", i);
        if ((f1 = filter_alloc(name, "qlafilter", &empty)) == NULL)
        {
            fprintf(stderr,
                    "filter_alloc: test 3 failed with %s.\n",
                    name);
            return 1;
        }
    }
    for (i = 0; i < n_filters; i++)
    {
        sprintf(name, "filter%d", i);
        if (!(f1 = filter_find(name)))
        {
            fprintf(stderr, "filter_find: test 3 failed.\n");
            return 1;
        }
    }
    for (i = 0; i < n_filters; i++)
    {
        sprintf(name, "filter%d", i);
        if (!(f1 = filter_find(name)))
        {
            fprintf(stderr, "filter_find: test 3 failed.\n");
            return 1;
        }
        filter_free(f1);
        if ((f1 = filter_find(name)))
        {
            fprintf(stderr,
                    "filter_find: test 3 failed - found deleted filter.\n");
            return 1;
        }
    }

    return 0;
}

int main(int argc, char** argv)
{
    int result = 0;
    set_libdir(MXS_STRDUP_A("../../modules/filter/qlafilter/"));
    mxs_log_init(NULL, NULL, MXS_LOG_TARGET_STDOUT);

    result += test1();
    result += test2();
    result += test3();

    mxs_log_finish();

    exit(result);
}
