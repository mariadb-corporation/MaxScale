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

class CsMonitorServer : public maxscale::MonitorServer
{
public:
    CsMonitorServer(const CsMonitorServer&) = delete;
    CsMonitorServer& operator=(const CsMonitorServer&) = delete;

    CsMonitorServer(SERVER* pServer,
                    const SharedSettings& shared,
                    int64_t admin_port);
    virtual ~CsMonitorServer();

    const char* name() const
    {
        return this->server->name();
    }

    bool ping(json_t** ppError = nullptr);

    bool refresh_config(json_t** ppError = nullptr);

    bool set_config(const std::string& body, json_t** ppError = nullptr);

    json_t* config() const
    {
        return m_sConfig.get();
    }

private:
    int64_t                 m_admin_port;
    std::unique_ptr<json_t> m_sConfig;
    std::unique_ptr<xmlDoc> m_sDoc;
};
