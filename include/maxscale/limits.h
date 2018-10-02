/*
 * Copyright (c) 2018 MariaDB Corporation Ab
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
#pragma once

/**
 * @file limits.h
 *
 * This file contains defines for hard limits of MaxScale.
 */

#include <maxscale/cdefs.h>

MXS_BEGIN_DECLS


/**
 * MXS_SO_RCVBUF
 *
 * The size of the network input buffer.
 */
#define MXS_SO_RCVBUF_SIZE (128 * 1024)

/**
 * MXS_SO_SNDBUF
 *
 * The size of the network output buffer.
 */
#define MXS_SO_SNDBUF_SIZE (128 * 1024)

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
 * The maximum number of threads/workers.
 */
#define MXS_MAX_THREADS 128

/**
 * MXS_MAX_ROUTING_THREADS
 *
 * The maximum number of routing threads/workers.
 */
#define MXS_MAX_ROUTING_THREADS 100

MXS_END_DECLS
