/*
 * This file is distributed as part of the MariaDB Corporation MaxScale.  It is free
 * software: you can redistribute it and/or modify it under the terms of the
 * GNU General Public License as published by the Free Software Foundation,
 * version 2.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Copyright MariaDB Corporation Ab 2013-2014
 */

/**
 * @file random.c  -  Random number generator for the MariaDB Corporation MaxScale
 *
 * @verbatim
 * Revision History
 *
 * Date		Who		Description
 * 26/08/15	Martin Brampton	Initial implementation
 *
 * @endverbatim
 */

#include <stdbool.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <random.h>

/* Public domain code for JKISS RNG - Comments added */
static unsigned int x = 123456789,y = 987654321,z = 43219876,c = 6543217; /* Seed variables */
static bool init = false;

/***
 * 
 * Return a random number
 * 
 * @return  uint    Random number
 * 
 */
unsigned int 
random_jkiss(void)
{
    unsigned long long t;
    if (!init) random_init_jkiss();
    x = 314527869 * x + 1234567;
    y ^= y << 5; y ^= y >> 7; y ^= y << 22;
    t = 4294584393ULL * z + c; c = t >> 32; z = t;
    return x + y + z;
}

/* Own code adapted from http://www0.cs.ucl.ac.uk/staff/d.jones/GoodPracticeRNG.pdf */

/***
 * 
 * Obtain a seed random number from /dev/urandom if available.
 * Otherwise use constant values
 * 
 * @return  uint    Random number
 * 
 */
static unsigned int 
random_devrand()
{
    int fn;
    unsigned int r;
    fn = open("/dev/urandom", O_RDONLY);
    if (fn == -1) return 0;
    if (read(fn, &r, 4) != 4) return 0;
    close(fn);
    return r;
}

/***
 * 
 * Initialise the generator using /dev/urandom if available, and warm up
 * with 1000 iterations
 * 
 */
static void
random_init_jkiss()
{
    int newrand, i;
    if ((newrand = random_devrand()) != 0) x = newrand;
    if ((newrand = random_devrand()) != 0) y = newrand;
    if ((newrand = random_devrand()) != 0) z = newrand;
    if ((newrand = random_devrand()) != 0) 
        c = newrand % 698769068 + 1; /* Should be less than 698769069 */
    for (i = 0; i < 1000; i++) random_jkiss();
}