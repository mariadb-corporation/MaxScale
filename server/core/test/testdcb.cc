/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2020-01-01
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
 * 05-09-2014   Martin Brampton     Initial implementation
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

#include <maxscale/config.h>
#include <maxscale/dcb.h>
#include <maxscale/listener.h>

/**
 * test1    Allocate a dcb and do lots of other things
 *
  */
static int
test1()
{
    DCB   *dcb;
    SERV_LISTENER dummy;
    /* Single buffer tests */
    ss_dfprintf(stderr, "testdcb : creating buffer with type DCB_ROLE_SERVICE_LISTENER");
    dcb = dcb_alloc(DCB_ROLE_INTERNAL, &dummy);
    printDCB(dcb);
    ss_info_dassert(dcb_isvalid(dcb), "New DCB must be valid");
    ss_dfprintf(stderr, "\t..done\nAllocated dcb.");
    printAllDCBs();
    ss_dfprintf(stderr, "\t..done\n");
    dcb->state = DCB_STATE_POLLING;
    dcb_close(dcb);
    ss_dfprintf(stderr, "Freed original dcb");
    ss_info_dassert(!dcb_isvalid(dcb), "Closed DCB must not be valid");
    ss_dfprintf(stderr, "\t..done\nProcess the zombies list");
    dcb_process_zombies(0);
    ss_dfprintf(stderr, "\t..done\n");

    return 0;
}

int main(int argc, char **argv)
{
    int result = 0;
    MXS_CONFIG* glob_conf = config_get_global_options();
    glob_conf->n_threads = 1;
    dcb_global_init();

    result += test1();

    exit(result);
}



