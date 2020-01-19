/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2023-12-18
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#define MXS_MODULE_NAME "kafkacdc"

#include <maxscale/ccdefs.hh>
#include <maxscale/router.hh>

#include "../replicator/replicator.hh"

// Never used
class KafkaCDCSession : public mxs::RouterSession
{
};

class KafkaCDC : public mxs::Router<KafkaCDC, KafkaCDCSession>
{
public:
    KafkaCDC(const KafkaCDC&) = delete;
    KafkaCDC& operator=(const KafkaCDC&) = delete;

    ~KafkaCDC() = default;

    // Router capabilities
    static constexpr uint64_t CAPS = RCAP_TYPE_RUNTIME_CONFIG;

    static KafkaCDC* create(SERVICE* pService, mxs::ConfigParameters* params)
    {
        return new KafkaCDC(pService, params);
    }

    KafkaCDCSession* newSession(MXS_SESSION* pSession, const Endpoints& endpoints)
    {
        return nullptr;
    }

    uint64_t getCapabilities()
    {
        return CAPS;
    }

    json_t* diagnostics() const;
    bool    configure(mxs::ConfigParameters* param);

private:
    KafkaCDC(SERVICE* pService, mxs::ConfigParameters* params);

    std::unique_ptr<cdc::Replicator> m_replicator;
};
