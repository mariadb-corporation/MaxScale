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
 * 18/08-2014   Mark Riddoch        Initial implementation
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
#include <math.h>
#include <time.h>

#include <maxscale/alloc.h>
#include <maxscale/atomic.h>
#include <maxscale/hashtable.h>

static void
read_lock(HASHTABLE *table)
{
    spinlock_acquire(&table->spin);
    while (table->writelock)
    {
        spinlock_release(&table->spin);
        while (table->writelock)
            ;
        spinlock_acquire(&table->spin);
    }
    table->n_readers++;
    spinlock_release(&table->spin);
}

static void
read_unlock(HASHTABLE *table)
{
    atomic_add(&table->n_readers, -1);
}

static int hfun(const void* key);
static int cmpfun(const void *, const void *);

static int hfun(const void* key)
{
    const int *i = (const int *)key;
    int j = (*i * 23) + 41;
    return j;
    /*    return *(int *)key;   */
}

static int cmpfun(const void* v1, const void* v2)
{
    int i1;
    int i2;

    i1 = *(const int *)v1;
    i2 = *(const int *)v2;

    return (i1 < i2 ? -1 : (i1 > i2 ? 1 : 0));
}

static double start;

/**
 * test1    spinlock_acquire_nowait tests
 *
 * Test that spinlock_acquire_nowait returns false if the spinlock
 * is already taken.
 *
 * Test that spinlock_acquire_nowait returns true if the spinlock
 * is not taken.
 *
 * Test that spinlock_acquire_nowait does hold the spinlock.
 */
static bool do_hashtest(
    int argelems,
    int argsize)
{
    bool       succp = true;
    HASHTABLE* h;
    int        nelems;
    int        i;
    int*       val_arr;
    int        hsize;
    int        longest;
    int*       iter;

    ss_dfprintf(stderr,
                "testhash : creating hash table of size %d, including %d "
                "elements in total, at time %g.",
                argsize,
                argelems,
                (double)clock() - start);

    val_arr = (int *)MXS_MALLOC(sizeof(void *)*argelems);
    MXS_ABORT_IF_NULL(val_arr);

    h = hashtable_alloc(argsize, hfun, cmpfun);

    ss_dfprintf(stderr, "\t..done\nAdd %d elements to hash table.", argelems);

    for (i = 0; i < argelems; i++)
    {
        val_arr[i] = i;
        hashtable_add(h, (void *)&val_arr[i], (void *)&val_arr[i]);
    }
    if (argelems > 1000)
    {
        ss_dfprintf(stderr, "\t..done\nOperation took %g", (double)clock() - start);
    }

    ss_dfprintf(stderr, "\t..done\nRead hash table statistics.");

    hashtable_get_stats((void *)h, &hsize, &nelems, &longest);

    ss_dfprintf(stderr, "\t..done\nValidate read values.");

    ss_info_dassert(hsize == (argsize > 0 ? argsize : 1), "Invalid hash size");
    ss_info_dassert((nelems == argelems) || (nelems == 0 && argsize == 0),
                    "Invalid element count");
    ss_info_dassert(longest <= nelems, "Too large longest list value");
    if (argelems > 1000)
    {
        ss_dfprintf(stderr, "\t..done\nOperation took %g", (double)clock() - start);
    }

    ss_dfprintf(stderr, "\t..done\nValidate iterator.");

    HASHITERATOR *iterator = hashtable_iterator(h);
    read_lock(h);
    for (i = 0; i < (argelems + 1); i++)
    {
        iter = (int *)hashtable_next(iterator);
        if (iter == NULL)
        {
            break;
        }
        if (argelems < 100)
        {
            ss_dfprintf(stderr, "\nNext item, iter = %d, i = %d", *iter, i);
        }
    }
    read_unlock(h);
    ss_info_dassert((i == argelems) || (i == 0 && argsize == 0), "\nIncorrect number of elements from iterator");
    hashtable_iterator_free(iterator);
    if (argelems > 1000)
    {
        ss_dfprintf(stderr, "\t..done\nOperation took %g", (double)clock() - start);
    }

    ss_dfprintf(stderr, "\t\t..done\n\nTest completed successfully.\n\n");

    hashtable_free(h);


    MXS_FREE(val_arr);
    return succp;
}

/**
 * @node Simple test which creates hashtable and frees it. Size and number of entries
 * sre specified by user and passed as arguments.
 *
 *
 * @return 0 if succeed, 1 if failed.
 *
 *
 * @details (write detailed description here)
 *
 */
int main(void)
{
    int rc = 1;
    start = (double) clock();

    if (!do_hashtest(0, 1))
    {
        goto return_rc;
    }
    if (!do_hashtest(10, 1))
    {
        goto return_rc;
    }
    if (!do_hashtest(1000, 10))
    {
        goto return_rc;
    }
    if (!do_hashtest(10, 0))
    {
        goto return_rc;
    }
    if (!do_hashtest(10, -5))
    {
        goto return_rc;
    }
    if (!do_hashtest(1500, 17))
    {
        goto return_rc;
    }
    if (!do_hashtest(1, 1))
    {
        goto return_rc;
    }
    if (!do_hashtest(10000, 133))
    {
        goto return_rc;
    }
    if (!do_hashtest(1000, 1000))
    {
        goto return_rc;
    }
    if (!do_hashtest(1000, 100000))
    {
        goto return_rc;
    }

    rc = 0;
return_rc:
    return rc;
}
