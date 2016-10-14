#pragma once
#ifndef _MAXSCALE_LIMITS_H
#define _MAXSCALE_LIMITS_H
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

#include <maxscale/cdefs.h>

MXS_BEGIN_DECLS

// This file defines hard limits of MaxScale.

// Thread information is stored in a bitmask whose size must be a
// multiple of 8. The bitmask is indexed using the thread id that start
// from 1. Hence, the hard maximum number of threads must be a
// multiple of 8 minus 1.
#define MXS_MAX_THREADS 255

MXS_END_DECLS

#endif
