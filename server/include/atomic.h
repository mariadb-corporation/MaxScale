#ifndef _ATOMIC_H
#define _ATOMIC_H
/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl.
 *
 * Change Date: 2019-01-01
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

#include <stdint.h>

#ifdef __cplusplus
extern "C" int atomic_add(int *variable, int value);
extern "C" int64_t atomic_add_int64(int64_t *variable, int64_t value);
#else
extern int atomic_add(int *variable, int value);
extern int64_t atomic_add_int64(int64_t *variable, int64_t value);
#endif
#endif
