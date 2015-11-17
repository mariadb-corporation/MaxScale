/*
 * This file is distributed as part of MaxScale.  It is free
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
 * Copyright MariaDB Corporation Ab 2014
 */

/**
 *
 * @verbatim
 * Revision History
 *
 * Date		Who			Description
 * 13-10-2014	Martin Brampton		Initial implementation
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

#include <gwbitmask.h>

#include <skygw_debug.h>

/**
 * test1	Allocate table of users and mess around with it
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
        ss_info_dassert(BIT_LENGTH_INITIAL == bitmask.length, "Length should be initial length.");
        for (i = 0; i < BIT_LENGTH_INITIAL; i++) {
            ss_info_dassert(0 == bitmask_isset(&bitmask, i), "All bits should initially be zero");
        }
        ss_info_dassert(0 != bitmask_isallclear(&bitmask), "Should be all clear");
        ss_dfprintf(stderr, "\t..done\nSet an arbitrary bit.");
        bitmask_set(&bitmask, 17);
        bitmask_copy(&another, &bitmask);
        ss_info_dassert(0 != bitmask_isset(&another, 17), "Test bit should be set");
        ss_dfprintf(stderr, "\t..done\nClear the arbitrary bit.");
        bitmask_clear(&bitmask, 17);
        ss_info_dassert(0 == bitmask_isset(&bitmask, 17), "Test bit should be clear");
        ss_info_dassert(0 != bitmask_isallclear(&bitmask), "Should be all clear");
        ss_dfprintf(stderr, "\t..done\nFree the bitmask.");
        bitmask_free(&bitmask);
        ss_info_dassert(0 == bitmask.length, "Length should be zero after bit mask freed.");
        ss_dfprintf(stderr, "\t..done\n");
		
	return 0;
        
}

int main(int argc, char **argv)
{
int	result = 0;

	result += test1();

	exit(result);
}

