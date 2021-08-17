/*
 * Copyright (c) 2021 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-08-17
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#pragma once

#include <string>
#include <vector>

/** The concrete implementations of get_canonical */
namespace maxsimd
{
namespace generic
{
std::string* get_canonical_impl(std::string* pSql, std::vector<const char*>* pMarkers);
}
}

namespace maxsimd
{
namespace simd256
{
std::string* get_canonical_impl(std::string* pSql, std::vector<const char*>* pMarkers);
}
}
