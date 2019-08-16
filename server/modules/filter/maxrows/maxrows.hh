/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2023-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#define MXS_MODULE_NAME "maxrows"

#include <maxscale/ccdefs.hh>
#include <maxscale/filter.hh>

enum Mode
{
    EMPTY, ERR, OK
};

static const MXS_ENUM_VALUE mode_values[] =
{
    {"empty", Mode::EMPTY},
    {"error", Mode::ERR  },
    {"ok",    Mode::OK   },
    {NULL}
};

class MaxRows;

class MaxRowsSession : public maxscale::FilterSession
{
public:
    MaxRowsSession(const MaxRowsSession&) = delete;
    MaxRowsSession& operator=(const MaxRowsSession&) = delete;

    // Create a new filter session
    static MaxRowsSession* create(MXS_SESSION* pSession, SERVICE* pService, MaxRows* pFilter)
    {
        return new(std::nothrow) MaxRowsSession(pSession, pService, pFilter);
    }

    // Handle a query from the client
    int routeQuery(GWBUF* pPacket);

    // Handle a reply from server
    int clientReply(GWBUF* pPacket, const mxs::ReplyRoute& down, const mxs::Reply& reply);

private:
    // Used in the create function
    MaxRowsSession(MXS_SESSION* pSession, SERVICE* pService, MaxRows* pFilter)
        : FilterSession(pSession, pService)
        , m_instance(pFilter)
    {
    }

    MaxRows*    m_instance;
    mxs::Buffer m_buffer;   // Contains the partial resultset
    bool        m_collect {true};
};

class MaxRows : public maxscale::Filter<MaxRows, MaxRowsSession>
{
public:
    MaxRows(const MaxRows&) = delete;
    MaxRows& operator=(const MaxRows&) = delete;

    struct Config
    {
        Config(const MXS_CONFIG_PARAMETER* params)
            : max_rows(params->get_integer("max_resultset_rows"))
            , max_size(params->get_size("max_resultset_size"))
            , debug(params->get_integer("debug"))
            , mode(static_cast<Mode>(params->get_enum("max_resultset_return", mode_values)))
        {
        }

        uint32_t max_rows;
        uint32_t max_size;
        uint32_t debug;
        Mode     mode;
    };

    static constexpr uint64_t CAPABILITIES = RCAP_TYPE_REQUEST_TRACKING;

    // Creates a new filter instance
    static MaxRows* create(const char* name, MXS_CONFIG_PARAMETER* params)
    {
        return new(std::nothrow) MaxRows(name, params);
    }

    // Creates a new session for this filter
    MaxRowsSession* newSession(MXS_SESSION* session, SERVICE* service)
    {
        return MaxRowsSession::create(session, service, this);
    }

    // Print diagnostics to a DCB
    void diagnostics(DCB* dcb) const
    {
    }

    // Returns JSON form diagnostic data
    json_t* diagnostics_json() const
    {
        return nullptr;
    }

    // Get filter capabilities
    uint64_t getCapabilities()
    {
        return CAPABILITIES;
    }

    // Return reference to filter config
    const Config& config() const
    {
        return m_config;
    }

private:
    MaxRows(const char* name, const MXS_CONFIG_PARAMETER* params)
        : m_name(name)
        , m_config(params)
    {
    }

private:
    std::string m_name;
    Config      m_config;
};
