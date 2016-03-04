#ifndef _MAXSCALE_H
#define _MAXSCALE_H
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
 * Copyright MariaDB Corporation Ab 2014
 */

/**
 * @file maxscale.h
 *
 * Some general definitions for MaxScale
 *
 * @verbatim
 * Revision History
 *
 * Date		Who		Description
 * 05/02/14	Mark Riddoch	Initial implementation
 *
 * @endverbatim
 */

#include <time.h>


/* Exit status for MaxScale */
#define	MAXSCALE_SHUTDOWN	0	/* Good shutdown */
#define MAXSCALE_BADCONFIG	1	/* Configuration fiel error */
#define MAXSCALE_NOLIBRARY	2	/* No embedded library found */
#define MAXSCALE_NOSERVICES	3	/* No servics are running */
#define MAXSCALE_ALREADYRUNNING	4	/* MaxScale is already runing */
#define MAXSCALE_BADARG		5	/* Bad command line argument */
#define MAXSCALE_INTERNALERROR	6	/* Internal error, see error log */

void maxscale_reset_starttime(void);
time_t maxscale_started(void);
int maxscale_uptime(void);

#endif
