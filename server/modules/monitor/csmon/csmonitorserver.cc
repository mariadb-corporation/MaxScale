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

#include "csmonitorserver.hh"
#include <maxbase/http.hh>
#include "columnstore.hh"

namespace http = mxb::http;
using std::unique_ptr;

CsMonitorServer::CsMonitorServer(SERVER* pServer,
                                 const SharedSettings& shared,
                                 int64_t admin_port)
    : mxs::MonitorServer(pServer, shared)
    , m_admin_port(admin_port)
{
}

CsMonitorServer::~CsMonitorServer()
{
}

bool CsMonitorServer::refresh_config(json_t** ppError)
{
    bool rv = false;
    http::Result result = http::get(cs::rest::create_url(*this->server, m_admin_port, cs::rest::CONFIG));

    if (result.code == 200)
    {
        rv = set_config(result.body, ppError);
    }
    else
    {
        PRINT_MXS_JSON_ERROR(ppError,
                             "Could not fetch config from '%s': %s",
                             this->server->name(), result.body.c_str());
    }

    return rv;
}

bool CsMonitorServer::refresh_status(json_t** ppError)
{
    bool rv = false;
    http::Result result = http::get(cs::rest::create_url(*this->server, m_admin_port, cs::rest::STATUS));

    if (result.code == 200)
    {
        rv = set_status(result.body, ppError);
    }
    else
    {
        PRINT_MXS_JSON_ERROR(ppError,
                             "Could not fetch status from '%s': %s",
                             this->server->name(), result.body.c_str());
    }

    return rv;
}

bool CsMonitorServer::set_config(const std::string& body, json_t** ppError)
{
    bool rv = false;

    json_error_t error;
    unique_ptr<json_t> sConfig(json_loadb(body.c_str(), body.length(), 0, &error));

    if (sConfig)
    {
        json_t* pConfig = json_object_get(sConfig.get(), cs::keys::CONFIG);

        if (pConfig)
        {
            const char* zXml = json_string_value(pConfig);
            size_t xml_len = json_string_length(pConfig);

            unique_ptr<xmlDoc> sDoc(xmlReadMemory(zXml, xml_len, "columnstore.xml", NULL, 0));

            if (sDoc)
            {
                m_sConfig = std::move(sConfig);
                m_sDoc = std::move(sDoc);

                rv = true;
            }
            else
            {
                PRINT_MXS_JSON_ERROR(ppError,
                                     "Failed to parse XML configuration of '%s'.", name());
            }
        }
        else
        {
            PRINT_MXS_JSON_ERROR(ppError,
                                 "Obtained config object from '%s', but it does not have a '%s' key.",
                                 name(),
                                 cs::keys::CONFIG);
        }
    }
    else
    {
        PRINT_MXS_JSON_ERROR(ppError, "Could not parse JSON data from: %s", error.text);
    }

    return rv;
}

bool CsMonitorServer::set_status(const std::string& body, json_t** ppError)
{
    bool rv = false;

    json_error_t error;
    unique_ptr<json_t> sStatus(json_loadb(body.c_str(), body.length(), 0, &error));

    if (sStatus)
    {
        json_t* pCluster_mode = json_object_get(sStatus.get(), cs::keys::CLUSTER_MODE);
        json_t* pDbrm_mode = json_object_get(sStatus.get(), cs::keys::DBRM_MODE);
        // TODO: 'dbroots' and 'services'.

        if (pCluster_mode && pDbrm_mode)
        {
            cs::ClusterMode cluster_mode;
            cs::DbrmMode dbrm_mode;

            const char* zCluster_mode = json_string_value(pCluster_mode);
            const char* zDbrm_mode = json_string_value(pDbrm_mode);

            if (cs::from_string(zCluster_mode, &cluster_mode) && cs::from_string(zDbrm_mode, &dbrm_mode))
            {
                m_status.cluster_mode = cluster_mode;
                m_status.dbrm_mode = dbrm_mode;
                m_status.sJson = std::move(sStatus);
                rv = true;
            }
            else
            {
                PRINT_MXS_JSON_ERROR(ppError,
                                     "Could not convert '%s' and/or '%s' to actual values.",
                                     zCluster_mode, zDbrm_mode);
            }
        }
        else
        {
            PRINT_MXS_JSON_ERROR(ppError,
                                 "Obtained status object from '%s', but it does not have the "
                                 "key '%s' and/or '%s': %s",
                                 name(),
                                 cs::keys::CLUSTER_MODE, cs::keys::DBRM_MODE, body.c_str());
        }
    }
    else
    {
        PRINT_MXS_JSON_ERROR(ppError, "Could not parse JSON data from: %s", error.text);
    }

    m_status.valid = rv;

    return rv;
}
