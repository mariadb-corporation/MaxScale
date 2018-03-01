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
#include <errno.h>
#include <maxscale/dcb.h>
#include <maxscale/listener.h>

#include "test_utils.h"

/**
 * test1    Allocate a service and do lots of other things
 *
  */

static int
test1()
{
    DCB     *dcb;
    int     result;
    int eno = 0;
    SERV_LISTENER dummy;

    /* Poll tests */
    ss_dfprintf(stderr,
                "testpoll : Initialise the polling system.");
    init_test_env(NULL);
    ss_dfprintf(stderr, "\t..done\nAdd a DCB");
    dcb = dcb_alloc(DCB_ROLE_CLIENT_HANDLER, &dummy);

    if (dcb == NULL)
    {
        ss_dfprintf(stderr, "\nError on function call: dcb_alloc() returned NULL.\n");
        return 1;
    }

    dcb->fd = socket(AF_UNIX, SOCK_STREAM, 0);

    if (dcb->fd < 0)
    {
        char errbuf[MXS_STRERROR_BUFLEN];
        ss_dfprintf(stderr, "\nError on function call: socket() returned %d: %s\n", errno, strerror_r(errno, errbuf,
                                                                                                      sizeof(errbuf)));
        return 1;
    }


    if ((eno = poll_add_dcb(dcb)) != 0)
    {
        ss_dfprintf(stderr, "\nError on function call: poll_add_dcb() returned %d.\n", eno);
        return 1;
    }

    if ((eno = poll_remove_dcb(dcb)) != 0)
    {
        ss_dfprintf(stderr, "\nError on function call: poll_remove_dcb() returned %d.\n", eno);
        return 1;
    }

    if ((eno = poll_add_dcb(dcb)) != 0)
    {
        ss_dfprintf(stderr, "\nError on function call: poll_add_dcb() returned %d.\n", eno);
        return 1;
    }

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

