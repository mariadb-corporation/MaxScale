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

#include <maxscale/cdefs.h>
#include <stdlib.h>
#include <string.h>
#include <maxbase/alloc.h>

MXS_BEGIN_DECLS

/**
 * @brief Abort the process if the pointer is NULL.
 *
 * To be used in circumstances where a memory allocation failure
 * cannot - currently - be dealt with properly.
 */
#define MXS_ABORT_IF_NULL(p) do {if (!p) {abort();}} while (false)

/**
 * @brief Abort the process if the provided value is non-zero.
 *
 * To be used in circumstances where a memory allocation or other
 * fatal error cannot - currently - be dealt with properly.
 */
#define MXS_ABORT_IF_TRUE(b) do {if (b) {abort();}} while (false)

/**
 * @brief Abort the process if the provided value is zero.
 *
 * To be used in circumstances where a memory allocation or other
 * fatal error cannot - currently - be dealt with properly.
 */
#define MXS_ABORT_IF_FALSE(b) do {if (!(b)) {abort();}} while (false)

MXS_END_DECLS
