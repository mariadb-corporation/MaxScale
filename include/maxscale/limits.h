#pragma once
/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2019-07-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * @file limits.h
 *
 * This file contains defines for hard limits of MaxScale.
 */

#include <maxscale/cdefs.h>

MXS_BEGIN_DECLS


/**
 * MXS_BACKEND_SO_RCVBUF
 *
 * The value used when setting SO_RCVBUF of backend sockets.
 */
#define MXS_BACKEND_SO_RCVBUF (128 * 1024)

/**
 * MXS_BACKEND_SO_SNDBUF
 *
 * The value used when setting SO_SNDBUF of backend sockets.
 */
#define MXS_BACKEND_SO_SNDBUF (128 * 1024)

/**
 * MXS_CLIENT_SO_RCVBUF
 *
 * The value used when setting SO_RCVBUF of client sockets.
 */
#define MXS_CLIENT_SO_RCVBUF  (128 * 1024)

/**
 * MXS_CLIENT_SO_SNDBUF
 *
 * The value used when setting SO_SNDBUF of client sockets.
 */
#define MXS_CLIENT_SO_SNDBUF  (128 * 1024)

/**
 * MXS_MAX_NW_READ_BUFFER_SIZE
 *
 * The maximum amount of data read in one gofrom a client DCB.
 *
 * TODO: Consider removing altogether so that we always read
 *       whatever is available in the socket.
 */
#define MXS_MAX_NW_READ_BUFFER_SIZE (32 * 1024)

/**
 * MXS_MAX_THREADS
 *
 * Thread information is stored in a bitmask whose size must be a
 * multiple of 8. The bitmask is indexed using the thread id that start
 * from 1. Hence, the hard maximum number of threads must be a
 * multiple of 8 minus 1.
 */
#define MXS_MAX_THREADS 255

MXS_END_DECLS
