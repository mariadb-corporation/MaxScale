/*
 * Copyright (c) 2021 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-12-13
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxsimd/multistmt.hh>
#include <maxbase/cpuinfo.hh>
#include "multistmt_impl.hh"

#include <string>

namespace maxsimd
{

namespace
{
const maxbase::CpuInfo& cpu_info {maxbase::CpuInfo::instance()};
}

#if defined (__x86_64__)
bool is_multi_stmt(const std::string& sql, Markers* pMarkers)
{
    if (cpu_info.has_avx2)
    {
        return simd256::is_multi_stmt_impl(sql, pMarkers);
    }
    else
    {
        return generic::is_multi_stmt_impl(sql);
    }
}
#else

bool is_multi_stmt(const std::string& sql, Markers*)
{
    return generic::is_multi_stmt_impl(sql);
}

#endif
}
