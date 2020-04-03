/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-03-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include "csmon.hh"
#include <maxscale/jansson.hh>
#include "csrest.hh"

class CsMonitorServer : public maxscale::MonitorServer
{
public:
    CsMonitorServer(const CsMonitorServer&) = delete;
    CsMonitorServer& operator=(const CsMonitorServer&) = delete;

    CsMonitorServer(SERVER* pServer,
                    const SharedSettings& shared,
                    int64_t admin_port);
    virtual ~CsMonitorServer();

    class Status
    {
    public:
        explicit operator bool () const
        {
            return this->valid;
        }

        bool                    valid        { false };
        cs::ClusterMode         cluster_mode { cs::READ_ONLY };
        cs::DbrmMode            dbrm_mode    { cs::SLAVE };
        std::unique_ptr<json_t> sJson;
    };

    const char* name() const
    {
        return this->server->name();
    }

    json_t* config() const
    {
        return m_sConfig.get();
    }

    const Status& status() const
    {
        return m_status;
    }

    bool ping(json_t** ppError = nullptr);

    bool refresh_config(json_t** ppError = nullptr);
    bool refresh_status(json_t** ppError = nullptr);

    bool set_config(const std::string& body, json_t** ppError = nullptr);
    bool set_status(const std::string& body, json_t** ppError = nullptr);

private:
    int64_t                 m_admin_port;
    std::unique_ptr<json_t> m_sConfig;
    std::unique_ptr<xmlDoc> m_sDoc;
    Status                  m_status;
};
