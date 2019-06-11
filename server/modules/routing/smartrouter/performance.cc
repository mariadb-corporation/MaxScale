/*
 * Copyright (c) 2019 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2022-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "performance.hh"

#include <cstdio>
#include <fstream>
#include <mutex>

const std::string file_version = "Alpha";   // if a file has a different version string, discard it.

CanonicalPerformance::CanonicalPerformance(const std::string& persistent_file)
    : m_persistent_file(persistent_file)
    , m_nChanges(0)
{
    read_persisted();
}

bool CanonicalPerformance::insert(const std::string& canonical, const PerformanceInfo& perf)
{
    bool saved = m_perfs.insert({canonical, perf}).second;
    m_nChanges += saved;

    return saved;
}

bool CanonicalPerformance::remove(const std::string& canonical)
{
    auto erased = m_perfs.erase(canonical);
    m_nChanges += erased;
    return erased;
}

PerformanceInfo CanonicalPerformance::find(const std::string& canonical)
{
    auto it = m_perfs.find(canonical);
    return it == m_perfs.end() ? PerformanceInfo() : it->second;
}

void CanonicalPerformance::clear()
{
    m_perfs.clear();
    std::remove(m_persistent_file.c_str());
    m_nChanges = 0;
}

// TODO, expensive. Saves the whole file whenever there are changes.
void CanonicalPerformance::persist() const
{
    if (m_nChanges == 0)
    {
        return;
    }

    std::ofstream out(m_persistent_file);
    if (!out)
    {
        MXS_ERROR("Could not open %s for writing", m_persistent_file.c_str());
    }

    out << file_version << '\n';

    for (const auto& e : m_perfs)
    {
        out << e.first << '\n';
        out << e.second.host() << '\n';
        out << e.second.duration().count() << '\n';
    }

    m_nChanges = 0;
}

void CanonicalPerformance::read_persisted()
{
    std::ifstream in(m_persistent_file);
    if (!in)
    {
        return;
    }

    std::string version;
    std::getline(in, version);
    if (version != file_version)
    {
        MXS_INFO("%s version does not match the expected one. Discarding file.", m_persistent_file.c_str());
        in.close();
        std::remove(m_persistent_file.c_str());
        return;
    }

    while (in)
    {
        std::string canonical;
        std::string host_str;
        std::string nano_str;
        std::getline(in, canonical);
        std::getline(in, host_str);
        std::getline(in, nano_str);

        if (!in)
        {
            break;
        }

        m_perfs.insert({canonical, {maxbase::Host(host_str), maxbase::Duration(std::stoull(nano_str))}});
    }

    m_nChanges = 0;
}

std::string show_some(const std::string& str, int nchars)
{
    int sz = str.length();
    if (sz > nchars)
    {
        return str.substr(0, nchars) + "...";
    }
    else
    {
        return str;
    }
}


// This needs TODO:
// 1. Read the file once at startup. There might also be a need to do cleanup
//    of the file if the configuration has changed.
// 2. Updates to the data should "quickly" become globally available,
//    rather than be written after every change.
// 3. Writing the file back should go through something like of lockless queue,
//    that triggers a write, possibly when there is less load on the system.
// 4. Every now and then some form of re-learn, maybe just dropping entries after
//    some expiration time.
// 5. If a host goes away (even for maintenance) entries associated with it should
//    probably be dropped immediately.
// 6. Save all data at shutdown.
// Start using xxhash
//
// Expiration rules. At least these rules should be implemented:
// 1. Since the various engines have different setup times during the first few queries,
//    this should be taken into account (not implemented).
// 2. Expire entries after X minutes.
// 3. If the measured time is very different from the stored one (+/20%),
//    expire the entry (not implemented).
// More rules can be found out by testing.


namespace
{
std::mutex canon_mutex;
const std::string persistent_file = "/tmp/max_canonical_perf.dat";      // TODO config
CanonicalPerformance& canon_store()
{
    // Note that the age of entries become "Now", the age was not written to file.
    static CanonicalPerformance cp(persistent_file);
    return cp;
}
}

PerformanceInfo perf_find(const std::string& canonical)
{
    std::unique_lock<std::mutex> guard(canon_mutex);
    auto perf = canon_store().find(canonical);

    if (perf.is_valid() && perf.age() > std::chrono::minutes(1))    // TODO to config, but not yet
    {
        canon_store().remove(canonical);
        return PerformanceInfo();
    }

    return perf;
}

bool perf_update(const std::string& canonical, const PerformanceInfo& perf)
{
    std::unique_lock<std::mutex> guard(canon_mutex);
    auto ret = canon_store().insert(canonical, perf);
    canon_store().persist();

    if (ret)
    {
        MXS_SDEBUG("Stored perf " << perf.duration() << ' ' << perf.host() << ' ' << show_some(canonical));
    }

    return ret;
}
