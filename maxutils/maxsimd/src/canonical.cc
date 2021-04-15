/*
 * Copyright (c) 2021 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-03-24
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxsimd/canonical.hh>
#include "impl/canonical_impl.hh"

#include <string>
#include <atomic>
#include <mutex>    // for call_once

namespace maxsimd
{

namespace
{
// TODO move this to maxbase init, add a CpuCapabilities class.
std::atomic<bool> avx2_supported {false};
std::once_flag flag;

bool is_avx_supported()
{
    std::call_once(flag, []() {
                       __builtin_cpu_init();
                       bool avx2 = __builtin_cpu_supports("avx2");
                       avx2_supported.store(avx2, std::memory_order_release);
                   });

    return avx2_supported.load(std::memory_order_relaxed);
}
}


std::string* get_canonical(std::string* pSql)
{
    // Simple version, for the future the capabilities etc need to be
    // a bit more accessible.
    if (is_avx_supported())
    {
        return maxsimd::simd256::get_canonical_impl(pSql);
    }
    else
    {
        return maxsimd::generic::get_canonical_impl(pSql);
    }
}
}
