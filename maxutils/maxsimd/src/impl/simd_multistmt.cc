/*
 * Copyright (c) 2021 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-09-20
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#if defined (__x86_64__)

#include "../multistmt_impl.hh"
#include "simd256.hh"
#include <maxbase/assert.h>

#include <string>
#include <functional>
#include <algorithm>
#include <vector>
#include <limits>

namespace maxsimd
{
namespace simd256
{
bool is_multi_stmt_impl(const std::string& sql, Markers* pMarkers)
{
    abort();
}
}
}
#endif
