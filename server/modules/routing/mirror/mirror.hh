/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2023-11-05
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/ccdefs.hh>

#include <maxscale/router.hh>
#include <maxscale/backend.hh>
#include <maxbase/shared_mutex.hh>

#include "exporter.hh"

class MirrorSession;

class Mirror : public mxs::Router<Mirror, MirrorSession>
{
public:
    Mirror(const Mirror&) = delete;
    Mirror& operator=(const Mirror&) = delete;

    ~Mirror() = default;
    static Mirror* create(SERVICE* pService, MXS_CONFIG_PARAMETER* params);
    MirrorSession* newSession(MXS_SESSION* pSession, const Endpoints& endpoints);
    json_t*        diagnostics() const;
    uint64_t       getCapabilities();
    bool           configure(MXS_CONFIG_PARAMETER* params);

    void ship(json_t* obj);

    mxs::Target* get_main() const
    {
        return m_main;
    }

private:
    Mirror(SERVICE* pService, MXS_CONFIG_PARAMETER* params, std::unique_ptr<Exporter> exporter);

    mxs::Target*              m_main;
    std::unique_ptr<Exporter> m_exporter;
    mxb::shared_mutex         m_rw_lock;
};
