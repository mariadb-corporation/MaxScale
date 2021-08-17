/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-08-17
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/ccdefs.hh>

#include <string>
#include <regex.h>

#include <maxscale/filter.hh>
#include <maxscale/service.hh>

#include "teesession.hh"

/**
 * The instance structure for the TEE filter - this holds the configuration
 * information for the filter.
 */
class Tee : public mxs::Filter<Tee, TeeSession>
{
    Tee(const Tee&);
    const Tee& operator=(const Tee&);
public:

    static Tee* create(const char* zName, mxs::ConfigParameters* ppParams);
    TeeSession* newSession(MXS_SESSION* session, SERVICE* service);
    json_t*     diagnostics() const;

    uint64_t getCapabilities()
    {
        return RCAP_TYPE_CONTIGUOUS_INPUT;
    }

    bool user_matches(const char* user) const
    {
        return m_user.length() == 0 || strcmp(user, m_user.c_str()) == 0;
    }

    bool remote_matches(const char* remote) const
    {
        return m_source.length() == 0 || strcmp(remote, m_source.c_str()) == 0;
    }

    mxs::Target* get_target() const
    {
        return m_target;
    }

    const mxb::Regex& get_match() const
    {
        return m_match;
    }

    const mxb::Regex& get_exclude() const
    {
        return m_exclude;
    }

    void set_enabled(bool value)
    {
        m_enabled = value;
    }

    bool is_enabled() const
    {
        return m_enabled;
    }

private:
    Tee(const char* name, mxs::ConfigParameters* params);

    std::string  m_name;
    mxs::Target* m_target;
    std::string  m_user;    /* The user name to filter on */
    std::string  m_source;  /* The source of the client connection */
    mxb::Regex   m_match;   /* Compiled match pattern */
    mxb::Regex   m_exclude; /* Compiled exclude pattern*/
    bool         m_enabled;
};
