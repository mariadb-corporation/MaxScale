/*
 * Copyright (c) 2021 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-02-11
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#pragma once

#include <string>
#include <vector>

/** The concrete implementations of is_multi_stmt */
namespace maxsimd
{
namespace generic
{
bool is_multi_stmt_impl(const std::string& sql);
}
}

namespace maxsimd
{
namespace simd256
{
bool is_multi_stmt_impl(const std::string& sql, std::vector<const char*>* pMarkers);
}
}
