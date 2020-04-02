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
#include "csrest.hh"

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

bool CsMonitorServer::set_config(const std::string& body, json_t** ppError)
{
    bool rv = false;

    json_error_t error;
    unique_ptr<json_t> sConfig(json_loadb(body.c_str(), body.length(), 0, &error));

    if (sConfig)
    {
        json_t* pColumnstore_config = json_object_get(sConfig.get(), cs::keys::CONFIG);

        if (pColumnstore_config)
        {
            const char* zXml = json_string_value(pColumnstore_config);
            size_t xml_len = json_string_length(pColumnstore_config);

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
