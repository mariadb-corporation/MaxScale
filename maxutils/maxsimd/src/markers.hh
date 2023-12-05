/*
 * Copyright (c) 2021 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#pragma once

#include <vector>
#include <cstdint>

namespace maxsimd
{
using Markers = std::vector<uint32_t>;

/**
 * Gets the thread-local marker vector. This space is temporarily used to store the points of interest in the
 * SQL strings. Using the same thread-local variable saves space that would otherwise be wasted by
 * implementing it internally in both the query canonicalization and multi-statement detection.
 */
Markers* markers();
}
