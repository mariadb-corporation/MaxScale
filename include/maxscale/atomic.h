#pragma once
#ifndef _MAXSCALE_ATOMIC_H
#define _MAXSCALE_ATOMIC_H
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
 * @file atomic.h The atomic operations used within the gateway
 *
 * @verbatim
 * Revision History
 *
 * Date         Who             Description
 * 10/06/13     Mark Riddoch    Initial implementation
 * 23/06/15     Martin Brampton Alternative for C++
 *
 * @endverbatim
 */

#include <maxscale/cdefs.h>

MXS_BEGIN_DECLS

int atomic_add(int *variable, int value);
int atomic_add(int *variable, int value);

MXS_END_DECLS

#endif
