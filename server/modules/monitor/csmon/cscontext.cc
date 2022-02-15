/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-02-11
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "cscontext.hh"

CsContext::CsContext(const std::string& name)
    : m_config(name)
{}

bool CsContext::configure(const mxs::ConfigParameters& params)
{
    bool rv = m_config.configure(params);

    if (rv)
    {
        m_http_config.headers["X-API-KEY"]    = m_config.api_key;
        m_http_config.headers["Content-Type"] = "application/json";

        // The CS daemon uses a self-signed certificate.
        m_http_config.ssl_verifypeer = false;
        m_http_config.ssl_verifyhost = false;

        m_manager = m_config.local_address;
    }

    return rv;
}
