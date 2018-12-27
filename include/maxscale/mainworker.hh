/*
 * Copyright (c) 2018 MariaDB Corporation Ab
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
#pragma once

#include <maxscale/ccdefs.hh>

#include <maxbase/worker.hh>

namespace maxscale
{

class MainWorker : public mxb::Worker
{
    MainWorker(const MainWorker&) = delete;
    MainWorker& operator=(const MainWorker&) = delete;

public:
    /**
     * Construct the main worker.
     *
     * @note There can be exactly one instance of @c MainWorker.
     */
    MainWorker();

    ~MainWorker();

    /**
     * Return the main worker.
     *
     * @return The main worker.
     */
    static MainWorker& get();

private:
    bool pre_run() override;
    void post_run() override;
    void epoll_tick() override;
};

}
