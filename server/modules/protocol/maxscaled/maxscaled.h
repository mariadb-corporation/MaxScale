#pragma once
#ifndef _MAXSCALED_H
#define _MAXSCALED_H
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
 * @file maxscaled.h The maxscaled protocol module header file
 *
 * @verbatim
 * Revision History
 *
 * Date     Who     Description
 * 13/06/14 Mark Riddoch    Initial implementation
 *
 * @endverbatim
 */
#include <maxscale/dcb.h>
#include <maxscale/spinlock.h>
#include <maxscale/housekeeper.h>

MXS_BEGIN_DECLS

/**
 * The maxscaled specific protocol structure to put in the DCB.
 */
typedef struct maxscaled
{
    SPINLOCK lock;      /**< Protocol structure lock */
    int      state;     /**< The connection state */
    char    *username;  /**< The login name of the user */
} MAXSCALED;

#define MAXSCALED_STATE_LOGIN   1   /**< Waiting for user */
#define MAXSCALED_STATE_PASSWD  2   /**< Waiting for password */
#define MAXSCALED_STATE_DATA    3   /**< User logged in */

MXS_END_DECLS

#endif
