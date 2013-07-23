#ifndef _SECRETS_H
#define _SECRETS_H
/*
 * This file is distributed as part of the SkySQL Gateway.  It is free
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
 * Copyright SkySQL Ab 2013
 */

/**
 * @file secrets.h 
 *
 * @verbatim
 * Revision History
 *
 * Date		Who			Description
 * 23/06/2013	Massimiliano Pinto	Initial implementation
 *
 * @endverbatim
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

#include <openssl/aes.h>

#define MAXSCALE_SECRETS_ONE 4
#define MAXSCALE_SECRETS_TWO 28
#define MAXSCALE_SECRETS_INIT_VAL_ONE 11
#define MAXSCALE_SECRETS_INIT_VAL_TWO 5

#endif
