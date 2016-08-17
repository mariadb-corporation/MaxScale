#ifndef _MAXSCALE_H
#define _MAXSCALE_H
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
 * @file maxscale.h
 *
 * Some general definitions for MaxScale
 *
 * @verbatim
 * Revision History
 *
 * Date     Who             Description
 * 05/02/14 Mark Riddoch    Initial implementation
 *
 * @endverbatim
 */

#include <time.h>


/* Exit status for MaxScale */
#define MAXSCALE_SHUTDOWN       0   /* Good shutdown */
#define MAXSCALE_BADCONFIG      1   /* Configuration fiel error */
#define MAXSCALE_NOLIBRARY      2   /* No embedded library found */
#define MAXSCALE_NOSERVICES     3   /* No servics are running */
#define MAXSCALE_ALREADYRUNNING 4   /* MaxScale is already runing */
#define MAXSCALE_BADARG         5   /* Bad command line argument */
#define MAXSCALE_INTERNALERROR  6   /* Internal error, see error log */

void maxscale_reset_starttime(void);
time_t maxscale_started(void);
int maxscale_uptime(void);

#endif
