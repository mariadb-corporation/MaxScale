#ifndef _ATOMIC_H
#define _ATOMIC_H
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

#ifdef __cplusplus
extern "C" int atomic_add(int *variable, int value);
#else
extern int atomic_add(int *variable, int value);
#endif
#endif
