#ifndef _ADMINUSERS_H
#define _ADMINUSERS_H
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
 * @file adminusers.h - Administration users support routines
 *
 * @verbatim
 * Revision History
 *
 * Date         Who             Description
 * 18/07/13     Mark Riddoch    Initial implementation
 *
 * @endverbatim
 */
#include <dcb.h>

#define ADMIN_SALT "MS"

extern int  admin_verify(char *, char *);
extern char *admin_add_user(char *, char *);
extern int  admin_search_user(char *);
extern void dcb_PrintAdminUsers(DCB *dcb);

char* admin_remove_user(char* uname, char* passwd);


#endif
