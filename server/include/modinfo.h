#ifndef _MODINFO_H
#define _MODINFO_H
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
 * @file modinfo.h The module information interface
 *
 * @verbatim
 * Revision History
 *
 * Date		Who		Description
 * 02/06/14	Mark Riddoch	Initial implementation
 *
 * @endverbatim
 */

/**
 * The status of the module. This gives some idea of the module
 * maturity.
 */
typedef enum {
	MODULE_IN_DEVELOPMENT = 0,
	MODULE_ALPHA_RELEASE,
	MODULE_BETA_RELEASE,
	MODULE_GA,
	MODULE_EXPERIMENTAL
} MODULE_STATUS;

/**
 * The API implemented by the module
 */
typedef enum {
	MODULE_API_PROTOCOL = 0,
	MODULE_API_ROUTER,
	MODULE_API_MONITOR,
	MODULE_API_FILTER,
	MODULE_API_AUTHENTICATION,
	MODULE_API_QUERY_CLASSIFIER,
} MODULE_API;

/**
 * The module version structure.
 *
 * The rules for changing these values are:
 *
 * Any change that affects an inexisting call in the API in question,
 * making the new API no longer compatible with the old,
 * must increment the major version.
 *
 * Any change that adds to the API, but does not alter the existing API
 * calls, must increment the minor version.
 *
 * Any change that is purely cosmetic and does not affect the calling
 * conventions of the API must increment only the patch version number.
 */
typedef struct {
	int		major;
	int		minor;
	int		patch;
} MODULE_VERSION;

/**
 * The module information structure
 */
typedef struct {
	MODULE_API	modapi;
	MODULE_STATUS	status;
	MODULE_VERSION	api_version;
	char		*description;
} MODULE_INFO;
#endif
