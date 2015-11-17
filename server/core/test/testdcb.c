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
 * 05-09-2014	Martin Brampton		Initial implementation
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

#include <dcb.h>

/**
 * test1	Allocate a dcb and do lots of other things
 *
  */
static int
test1()
{
DCB   *dcb, *extra, *clone;
int     size = 100;
int     bite1 = 35;
int     bite2 = 60;
int     bite3 = 10;
int     buflen;

        /* Single buffer tests */
        ss_dfprintf(stderr,
                    "testdcb : creating buffer with type DCB_ROLE_SERVICE_LISTENER"); 
        dcb = dcb_alloc(DCB_ROLE_SERVICE_LISTENER);
        printDCB(dcb);
        ss_info_dassert(dcb_isvalid(dcb), "New DCB must be valid");
        ss_dfprintf(stderr, "\t..done\nAllocated dcb.");
        clone = dcb_clone(dcb);
        ss_dfprintf(stderr, "\t..done\nCloned dcb");
        printAllDCBs();
        ss_info_dassert(true, "Something is true");
        ss_dfprintf(stderr, "\t..done\n");
        dcb_close(dcb);
        ss_dfprintf(stderr, "Freed original dcb");
        ss_info_dassert(!dcb_isvalid(dcb), "Freed DCB must not be valid");
        ss_dfprintf(stderr, "\t..done\nMake clone DCB a zombie");
        clone->state = DCB_STATE_NOPOLLING;
        dcb_close(clone);
        ss_info_dassert(dcb_get_zombies() == clone, "Clone DCB must be start of zombie list now");
        ss_dfprintf(stderr, "\t..done\nProcess the zombies list");
        dcb_process_zombies(0);
        ss_dfprintf(stderr, "\t..done\nCheck clone no longer valid");
        ss_info_dassert(!dcb_isvalid(clone), "After zombie processing, clone DCB must not be valid");
        ss_dfprintf(stderr, "\t..done\n");
		
	return 0;
}

int main(int argc, char **argv)
{
int	result = 0;

	result += test1();

	exit(result);
}



