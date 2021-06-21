/*
 * Copyright (c) 2021 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-06-21
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <string>
#include <vector>

namespace maxbase
{

/** CpuInfo is used to determine SIMD capabilities,
 *  but contains other cpu information as well.
 *  The class is a simple Meyers singleton and is
 *  not modified (is const) after initial initialization.
 */
class CpuInfo
{
public:
    static const CpuInfo& instance();

    int              cache_line_size;
    std::vector<int> cache_size;    // Cache sizes, L1 is cache_size[0].

    std::string cpu_model_name;
    std::string cpu_vendor_id;
    std::string num_cores;
    std::string num_hw_threads;

    bool has_mmx;
    bool has_sse;
    bool has_sse2;
    bool has_sse4_1;
    bool has_sse4_2;
    bool has_sse4a;
    bool has_avx;
    bool has_avx2;
    bool has_avx512f;
    bool has_avx512cd;
    bool has_avx512dq;
    bool has_avx512pf;
    bool has_avx512er;
    bool has_avx512vl;
    bool has_avx512bw;
    bool has_avx512ifma;
    bool has_avx512vbmi;
    bool has_avx512vbmi2;
    bool has_avx512vaes;
    bool has_avx512bitalg;
    bool has_avx5124fmaps;
    bool has_avx512vpclmulqdq;
    bool has_avx512gfni;
    bool has_avx512_vnni;
    bool has_avx5124vnniw;
    bool has_avx512vpopcntdq;
    bool has_avx512_bf16;

    // Human readable basic information.
    std::string info_string() const;
private:
    CpuInfo();
    CpuInfo(const CpuInfo&&) = delete;
};
}
