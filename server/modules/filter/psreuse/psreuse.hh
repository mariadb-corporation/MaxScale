/*
 * Copyright (c) 2024 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl.
 *
 * Change Date: 2028-05-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#define MXB_MODULE_NAME "psreuse"

#include <maxscale/filter.hh>
#include <maxscale/protocol/mariadb/module_names.hh>
#include <maxscale/protocol/mariadb/mysql.hh>
#include <maxscale/protocol/mariadb/trackers.hh>
#include <unordered_map>

class PsReuse;

class PsReuseSession final : public maxscale::FilterSession
{
public:
    PsReuseSession(const PsReuseSession&) = delete;
    PsReuseSession& operator=(const PsReuseSession&) = delete;

    PsReuseSession(MXS_SESSION* pSession, SERVICE* pService, PsReuse& filter);

    bool routeQuery(GWBUF&& packet) override;
    bool clientReply(GWBUF&& packet, const mxs::ReplyRoute& down, const mxs::Reply& reply) override;

private:
    struct CacheEntry
    {
        GWBUF    buffer;
        uint32_t id;
        bool     active = false;
    };

    using PsCache = std::unordered_map<std::string, CacheEntry>;
    using IdMap = std::unordered_map<uint32_t, std::reference_wrapper<CacheEntry>>;

    PsReuse&                  m_filter;
    PsCache                   m_ps_cache;
    IdMap                     m_ids;
    std::string               m_current_sql;
    uint32_t                  m_prev_id = 0;
    mariadb::MultiPartTracker m_tracker;
};

class PsReuse final : public mxs::Filter
{
public:
    PsReuse(const PsReuse&) = delete;
    PsReuse& operator=(const PsReuse&) = delete;

    static PsReuse* create(const char* name)
    {
        return new PsReuse(name);
    }

    std::shared_ptr<mxs::FilterSession> newSession(MXS_SESSION* pSession, SERVICE* pService) override
    {
        return std::make_shared<PsReuseSession>(pSession, pService, *this);
    }

    json_t* diagnostics() const override;

    uint64_t getCapabilities() const override
    {
        return MXS_NO_MODULE_CAPABILITIES;
    }

    mxs::config::Configuration& getConfiguration() override
    {
        return m_config;
    }

    std::set<std::string> protocols() const override
    {
        return {MXS_MARIADB_PROTOCOL_NAME};
    }

    void hit()
    {
        m_hits.fetch_add(1, std::memory_order_relaxed);
    }

    void miss()
    {
        m_misses.fetch_add(1, std::memory_order_relaxed);
    }

private:
    PsReuse(const std::string& name);

    mxs::config::Configuration m_config;
    std::atomic<int64_t>       m_hits{0};
    std::atomic<int64_t>       m_misses{0};
};
