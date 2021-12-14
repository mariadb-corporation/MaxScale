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
#pragma once

#include <maxscale/ccdefs.hh>

#include "perf_info.hh"
#include <maxbase/gcupdater.hh>

class PerformanceInfoUpdater : public maxbase::GCUpdater<SharedPerformanceInfo>
{
public:
    PerformanceInfoUpdater();

private:
    PerformanceInfoContainer* create_new_copy(const PerformanceInfoContainer* pCurrent) override;

    void make_updates(PerformanceInfoContainer* pData,
                      std::vector<typename SharedPerformanceInfo::InternalUpdate>& queue) override;
};
