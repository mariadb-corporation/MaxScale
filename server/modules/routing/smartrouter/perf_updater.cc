/*
 * Copyright (c) 2019 MariaDB Corporation Ab
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

#include "perf_updater.hh"
#include <maxscale/config.hh>

PerformanceInfoUpdater::PerformanceInfoUpdater()
    : GCUpdater(new PerformanceInfoContainer(),
                config_threadcount(),
                5000,                           // config, maybe. 5000 should be a pretty safe queue size.
                3,                              // 3 copies. Not expecting the container to be very large.
                true)                           // order updates.
{
}

PerformanceInfoContainer* PerformanceInfoUpdater::create_new_copy(const PerformanceInfoContainer* pCurrent)
{
    return new PerformanceInfoContainer {*pCurrent};
}

void PerformanceInfoUpdater::make_updates(PerformanceInfoContainer* pData,
                                          std::vector<typename SharedPerformanceInfo::InternalUpdate>& queue)
{
    for (const auto& e : queue)
    {
        auto res = pData->emplace(e.update.key, e.update.value);
        if (!res.second)
        {
            res.first->second = e.update.value;
        }
    }
}
