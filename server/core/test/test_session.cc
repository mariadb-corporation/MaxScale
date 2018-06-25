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
 * 11-09-2014   Martin Brampton     Initial implementation
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

#include <maxscale/poll.h>
#include <dcb.h>

/**
 * test1    Allocate a service and do lots of other things
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
    int result = 0;

    result += test1();

    exit(result);
}

