/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl.
 *
 * Change Date: 2019-07-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * @file gw_utils.c - A set if utility functions useful within the context
 * of the gateway.
 *
 * @verbatim
 * Revision History
 *
 * Date         Who                     Description
 * 03-06-2013   Massimiliano Pinto      gateway utils
 * 12-06-2013   Massimiliano Pinto      gw_read_gwbuff
 *                                      with error detection
 *                                      and its handling
 * 01-07-2013   Massimiliano Pinto      Removed session->backends
 *                                      from gw_read_gwbuff()
 * 25-09-2013   Massimiliano Pinto      setipaddress uses getaddrinfo
 * 06-02-2014   Mark Riddoch            Added parse_bindconfig
 * 10-02-2014   Massimiliano Pinto      Added return code to setipaddress
 * 02-09-2014   Martin Brampton         Replace C++ comment with C comment
 * 02-03-2016   Martin Brampton         Remove default from parse_bindconfig
 *
 *@endverbatim
 */

#include <maxscale/gw.h>
#include <maxscale/dcb.h>
#include <maxscale/session.h>
#include <maxscale/log_manager.h>

/**
 * Daemonize the process by forking and putting the process into the
 * background.
 */
bool gw_daemonize(void)
{
    pid_t pid;

    pid = fork();

    if (pid < 0)
    {
        char errbuf[MXS_STRERROR_BUFLEN];
        fprintf(stderr, "fork() error %s\n", strerror_r(errno, errbuf, sizeof(errbuf)));
        exit(1);
    }

    if (pid != 0)
    {
        /* exit from main */
        return true;
    }

    if (setsid() < 0)
    {
        char errbuf[MXS_STRERROR_BUFLEN];
        fprintf(stderr, "setsid() error %s\n", strerror_r(errno, errbuf, sizeof(errbuf)));
        exit(1);
    }
    return false;
}
