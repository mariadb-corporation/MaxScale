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
 * 11-09-2014	Martin Brampton		Initial implementation
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

#include <poll.h>
#include <dcb.h>

/**
 * test1	Allocate a service and do lots of other things
 *
  */

static int
test1()
{
DCB     *dcb;
int     result;

        /* Poll tests */  
        ss_dfprintf(stderr,
                    "testpoll : Initialise the polling system."); 
        poll_init();
        ss_dfprintf(stderr, "\t..done\nAdd a DCB");
        dcb = dcb_alloc(DCB_ROLE_SERVICE_LISTENER);
        dcb->fd = socket(AF_UNIX, SOCK_STREAM, 0);
        poll_add_dcb(dcb);
        poll_remove_dcb(dcb);
        poll_add_dcb(dcb);
        ss_dfprintf(stderr, "\t..done\nStart wait for events.");
        sleep(10);
        poll_shutdown();
        ss_dfprintf(stderr, "\t..done\nTidy up.");
        dcb_close(dcb);
        ss_dfprintf(stderr, "\t..done\n");
		
	return 0;
        
}

int main(int argc, char **argv)
{
int	result = 0;

	result += test1();

	exit(result);
}

