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

#include <maxsimd/multistmt.hh>
#include <maxbase/assert.h>

#include <string>
#include <functional>
#include <algorithm>
#include <vector>
#include <limits>

namespace maxsimd
{
namespace generic
{
bool is_multi_stmt_impl(const std::string& sql, Markers* pMarkers)
{
    abort();
}
}
}
