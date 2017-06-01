#pragma once
/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2020-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/cppdefs.hh>

#include <string>
#include <regex.h>

#include <maxscale/filter.hh>
#include <maxscale/service.h>

#include "teesession.hh"

/**
 * The instance structure for the TEE filter - this holds the configuration
 * information for the filter.
 */
class Tee: public mxs::Filter<Tee, TeeSession>
{
    Tee(const Tee&);
    const Tee& operator=(const Tee&);
public:

    static Tee* create(const char* zName, char** pzOptions, MXS_CONFIG_PARAMETER* ppParams);
    TeeSession* newSession(MXS_SESSION* session);
    void diagnostics(DCB* pDcb);
    json_t* diagnostics_json() const;

    uint64_t getCapabilities()
    {
        return RCAP_TYPE_CONTIGUOUS_INPUT;
    }

    bool user_matches(const char* user)const
    {
        return m_user.length() == 0 || strcmp(user, m_user.c_str()) == 0;
    }

    bool remote_matches(const char* remote)const
    {
        return m_source.length() == 0 || strcmp(remote, m_source.c_str()) == 0;
    }

    SERVICE* get_service() const
    {
        return m_service;
    }

private:
    Tee(SERVICE* service, const char* user, const char* remote,
        const char* match, const char* nomatch, int cflags);

    SERVICE*    m_service;
    std::string m_user; /* The user name to filter on */
    std::string m_source; /* The source of the client connection */
    std::string m_match; /* Optional text to match against */
    std::string m_nomatch; /* Optional text to match against for exclusion */
    regex_t m_re; /* Compiled regex text */
    regex_t m_nore; /* Compiled regex nomatch text */
};
