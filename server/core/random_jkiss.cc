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
 * @file random_jkiss.c  -  Random number generator for the MariaDB Corporation MaxScale
 *
 * See http://www0.cs.ucl.ac.uk/staff/d.jones/GoodPracticeRNG.pdf for discussion of random
 * number generators (RNGs).
 */

#include <stdbool.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <maxscale/random_jkiss.h>
#include <maxscale/debug.h>

/* Public domain code for JKISS RNG - Comment header added */

/* If possible, the seed variables will be set from /dev/urandom but
 * should that fail, these arbitrary numbers will be used as a last resort.
 */
static unsigned int x = 123456789, y = 987654321, z = 43219876, c = 6543217; /* Seed variables */
static bool init = false;

unsigned int random_jkiss(void)
{
    unsigned long long t;
    unsigned int result;
    ss_dassert(init);

    x = 314527869 * x + 1234567;
    y ^= y << 5;
    y ^= y >> 7;
    y ^= y << 22;
    t = 4294584393ULL * z + c;
    c = t >> 32;
    z = t;
    result = x + y + z;
    return result;
}

/* Own code adapted from http://www0.cs.ucl.ac.uk/staff/d.jones/GoodPracticeRNG.pdf */

/***
 *
 * Obtain a seed random number from /dev/urandom if available.
 *
 * @return  uint    Random number
 *
 */
static unsigned int random_jkiss_devrand(void)
{
    int fn;
    unsigned int r;

    if ((fn = open("/dev/urandom", O_RDONLY)) == -1)
    {
        return 0;
    }

    if (read(fn, &r, sizeof(r)) != sizeof(r))
    {
        r = 0;
    }
    close(fn);
    return r;
}

void random_jkiss_init(void)
{
    if (!init)
    {
        int newrand, i;

        if ((newrand = random_jkiss_devrand()) != 0)
        {
            x = newrand;
        }

        if ((newrand = random_jkiss_devrand()) != 0)
        {
            y = newrand;
        }

        if ((newrand = random_jkiss_devrand()) != 0)
        {
            z = newrand;
        }

        if ((newrand = random_jkiss_devrand()) != 0)
        {
            c = newrand % 698769068 + 1; /* Should be less than 698769069 */
        }

        init = true;
    }
}
