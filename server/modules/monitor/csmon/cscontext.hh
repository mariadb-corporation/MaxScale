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
#pragma once

#include "csmon.hh"
#include <maxbase/http.hh>
#include "csconfig.hh"

class CsContext
{
public:
    /**
     * @param name  The name of the Columnstore configuration object.
     */
    CsContext(const std::string& name, std::function<bool()> cb);

    bool configure(const mxs::ConfigParameters& params);

    int revision() const
    {
        return m_revision;
    }

    const std::string& manager() const
    {
        return m_manager;
    }

    void set_manager(const std::string& manager)
    {
        m_manager = manager;
    }

    const CsConfig& config() const
    {
        return m_config;
    }

    CsConfig& config()
    {
        return m_config;
    }

    const mxb::http::Config& http_config() const
    {
        return m_http_config;
    }

    mxb::http::Config http_config(const std::chrono::seconds& timeout) const
    {
        mxb::http::Config http_config(m_http_config);

        // We set the timeout to larger than the timeout specified to the
        // Columnstore daemon, so that the timeout surely expires first in
        // the daemon and only then in the HTTP library.
        http_config.timeout = timeout + std::chrono::seconds(mxb::http::DEFAULT_TIMEOUT);
        return http_config;
    }

    int current_trx_id() const
    {
        return m_next_trx_id;
    }

    int next_trx_id()
    {
        return ++m_next_trx_id;
    }

private:
    CsConfig          m_config;
    mxb::http::Config m_http_config;
    std::string       m_manager;
    int               m_revision { 1 };
    int               m_next_trx_id { 0 };
};
