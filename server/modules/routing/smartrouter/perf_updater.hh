/*
 * Copyright (c) 2019 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/ccdefs.hh>

#include "perf_info.hh"
#include <maxbase/gcupdater.hh>
#include <maxscale/routingworker.hh>

class PerformanceInfoUpdater : public maxbase::GCUpdater<SharedPerformanceInfo>
                             , private maxscale::RoutingWorker::Data
{
public:
    PerformanceInfoUpdater();
private:
    PerformanceInfoContainer* create_new_copy(const PerformanceInfoContainer* pCurrent) override;

    void make_updates(PerformanceInfoContainer* pData,
                      std::vector<typename SharedPerformanceInfo::InternalUpdate>& queue) override;

    void init_for(maxscale::RoutingWorker* pWorker) override final;
    void finish_for(maxscale::RoutingWorker* pWorker) override final;
};
