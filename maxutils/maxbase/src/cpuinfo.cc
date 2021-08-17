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

#include <maxbase/cpuinfo.hh>
#include <maxbase/string.hh>
#include <maxbase/pretty_print.hh>
#include <iostream>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <map>
#include <set>

using namespace maxbase;
using namespace std::string_literals;

namespace
{

// Base path to cpu cache info, "/index" is followed by a digit/file-name.
constexpr const char* const cache_base_path = "/sys/devices/system/cpu/cpu0/cache/index";

// size of data cache-line in bytes
int get_cache_line_size()
{
    int sz = 0;
    std::string file_name {cache_base_path + "0/coherency_line_size"s};
    std::ifstream is(file_name);

    if (is)
    {
        is >> sz;
    }

    return sz;
}

// number of data caches (L1, L2,...)
int get_num_caches()
{
    int num {};

    for (int i {};; ++i)
    {
        std::string dir {cache_base_path + std::to_string(i) + "/"};
        std::ifstream is(dir + "type");

        if (!is)
        {
            return num;
        }

        std::string type;
        is >> type;

        if (type == "Instruction")
        {
            continue;
        }

        ++num;
    }
}

// Returns cache size. Levels are indexed from 0 (so L1 is index 0).
// Returns 0 when level does not exist (so expect 0-2 to have values).
int get_cache_size(int requested_level)
{
    ++requested_level;

    for (int i = requested_level - 1;; ++i)
    {
        std::string type_name {cache_base_path + std::to_string(i) + "/type"};
        std::ifstream type_file(type_name);
        if (!type_file)
        {
            return 0;
        }
        std::string type;
        type_file >> type;
        if (type == "Instruction")
        {
            continue;
        }

        std::string level_name {cache_base_path + std::to_string(i) + "/level"};

        std::ifstream level_file(level_name);
        if (!level_file)
        {
            return 0;
        }
        int level;
        level_file >> level;
        if (level != requested_level)
        {
            continue;
        }

        std::string size_name {cache_base_path + std::to_string(i) + "/size"};
        std::ifstream size_file(size_name);
        if (!size_file)
        {
            return 0;
        }

        int sz;
        char unit;
        size_file >> sz;
        size_file >> unit;

        switch (unit)
        {
        case 'K':
            sz *= 1024;
            break;

        case 'M':
            sz *= 1024 * 1024;
            break;

        case 'G':
            sz *= 1024 * 1024 * 1024;
            break;

        default:
            sz = 0;
        }

        return sz;
    }
}
}

namespace maxbase
{
CpuInfo::CpuInfo()
{
    // Cpu cache data
    cache_line_size = get_cache_line_size();
    int level = 0;
    while (auto sz = get_cache_size(level++))
    {
        cache_size.push_back(sz);
        ++sz;
    }

    // Read /proc/cpuinfo into a map
    std::ifstream cpu_info("/proc/cpuinfo");
    std::map<std::string, std::string> map;

    for (std::string line; std::getline(cpu_info, line);)
    {
        auto pos = line.find(':');
        if (pos && pos != std::string::npos)
        {
            auto key = trimmed_copy(line.substr(0, pos));
            auto val = trimmed_copy(line.substr(pos + 1));
            map.insert(std::make_pair(key, val));
        }
    }

    // Set some proc strings
    cpu_vendor_id = map["vendor_id"];
    cpu_model_name = map["model name"];
    num_hw_threads = map["siblings"];
    num_cores = map["cpu cores"];

    // Set flags
    std::istringstream flag_stream {map["flags"]};
    std::set<std::string> flags;
    std::string f;
    while (flag_stream)
    {
        flag_stream >> f;
        flags.insert(f);
    }

    auto set_flag = [&flags](const std::string& name, bool& flag) {
            flag = (flags.find(name) != end(flags));
        };

    set_flag("mmx", has_mmx);
    set_flag("sse", has_sse);
    set_flag("sse2", has_sse2);
    set_flag("sse4_1", has_sse4_1);
    set_flag("sse4_2", has_sse4_2);
    set_flag("sse4a", has_sse4a);
    set_flag("avx", has_avx);
    set_flag("avx2", has_avx2);
    set_flag("avx512f", has_avx512f);
    set_flag("avx512cd", has_avx512cd);
    set_flag("avx512dq", has_avx512dq);
    set_flag("avx512pf", has_avx512pf);
    set_flag("avx512er", has_avx512er);
    set_flag("avx512vl", has_avx512vl);
    set_flag("avx512bw", has_avx512bw);
    set_flag("avx512ifma", has_avx512ifma);
    set_flag("avx512vbmi", has_avx512vbmi);
    set_flag("avx512vbmi2", has_avx512vbmi2);
    set_flag("avx512vaes", has_avx512vaes);
    set_flag("avx512bitalg", has_avx512bitalg);
    set_flag("avx5124fmaps", has_avx5124fmaps);
    set_flag("avx512vpclmulqdq", has_avx512vpclmulqdq);
    set_flag("avx512gfni", has_avx512gfni);
    set_flag("avx512_vnni", has_avx512_vnni);
    set_flag("avx5124vnniw", has_avx5124vnniw);
    set_flag("avx512vpopcntdq", has_avx512vpopcntdq);
    set_flag("avx512_bf16", has_avx512_bf16);
}

const CpuInfo& CpuInfo::instance()
{
    static CpuInfo cpu_info;
    return cpu_info;
}

std::string CpuInfo::info_string() const
{
    std::ostringstream os;

    os << "Cpu model    : " << cpu_model_name << '\n';
    os << "Cpu vendor   : " << cpu_vendor_id << '\n';
    os << "# cores      : " << num_cores << '\n';
    os << "# hw threads : " << num_hw_threads << '\n';
    os << "Cache line   : " << pretty_size(cache_line_size) << '\n';

    for (size_t i = 0; i < cache_size.size(); ++i)
    {
        os << "L" << i + 1 << "           : " << pretty_size(cache_size[i]) << '\n';
    }

    return os.str();
}
}
