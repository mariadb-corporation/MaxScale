/*
 * Copyright (c) 2021 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-10-29
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxsimd/canonical.hh>
#include <maxbase/cpuinfo.hh>
#include "canonical_impl.hh"

#include <string>

namespace maxsimd
{

namespace
{
const maxbase::CpuInfo& cpu_info {maxbase::CpuInfo::instance()};
}

#if defined (__x86_64__)
std::string* get_canonical(std::string* pSql, Markers* pMarkers)
{
    if (cpu_info.has_avx2)
    {
        return simd256::get_canonical_impl(pSql, pMarkers);
    }
    else
    {
        return generic::get_canonical_impl(pSql, pMarkers);
    }
}
#else

std::string* get_canonical(std::string* pSql, Markers* pMarkers)
{
    return generic::get_canonical_impl(pSql, pMarkers);
}

#endif
}
