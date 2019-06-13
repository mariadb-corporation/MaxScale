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

CanonicalPerformance::CanonicalPerformance()
    : m_nChanges(0)
{
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


// These are TODOs for the GA version. The Beta version will not have persistence.
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
