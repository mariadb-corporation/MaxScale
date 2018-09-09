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

#include <string>
#include <regex.h>

#include <maxscale/filter.hh>
#include <maxscale/service.h>

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

    static Tee* create(const char* zName, MXS_CONFIG_PARAMETER* ppParams);
    TeeSession* newSession(MXS_SESSION* session);
    void        diagnostics(DCB* pDcb);
    json_t*     diagnostics_json() const;

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

    SERVICE* get_service() const
    {
        return m_service;
    }

    pcre2_code* get_match() const
    {
        return m_match_code;
    }

    pcre2_code* get_exclude() const
    {
        return m_exclude_code;
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
    Tee(SERVICE* service,
        std::string user,
        std::string remote,
        pcre2_code* match,
        std::string match_string,
        pcre2_code* exclude,
        std::string exclude_string);

    SERVICE*    m_service;
    std::string m_user;         /* The user name to filter on */
    std::string m_source;       /* The source of the client connection */
    pcre2_code* m_match_code;   /* Compiled match pattern */
    pcre2_code* m_exclude_code; /* Compiled exclude pattern*/
    std::string m_match;        /* Pattern for matching queries */
    std::string m_exclude;      /* Pattern for excluding queries */
    bool        m_enabled;
};
