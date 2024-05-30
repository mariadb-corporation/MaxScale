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

#define MXB_MODULE_NAME "optimistictrx"

#include <maxscale/filter.hh>
#include <maxscale/queryclassifier.hh>
#include <maxbase/checksum.hh>
#include <maxbase/small_vector.hh>
#include <maxscale/protocol/mariadb/module_names.hh>
#include <maxscale/protocol/mariadb/mysql.hh>
#include <maxscale/protocol/mariadb/trackers.hh>
#include <deque>

class OptimisticTrx;

class OptimisticTrxSession final : public maxscale::FilterSession
{
public:
    OptimisticTrxSession(const OptimisticTrxSession&) = delete;
    OptimisticTrxSession& operator=(const OptimisticTrxSession&) = delete;

    OptimisticTrxSession(MXS_SESSION* pSession, SERVICE* pService, OptimisticTrx& filter);

    bool routeQuery(GWBUF&& packet) override;
    bool clientReply(GWBUF&& packet, const mxs::ReplyRoute& down, const mxs::Reply& reply) override;

private:
    enum class State {IDLE, COLLECT, IGNORE};
    enum ReplyAction : uint8_t {IGNORE, CHECKSUM, COMPARE, COMPLETE, DISCARD};

    bool state_idle(GWBUF&& packet);
    bool state_collect(GWBUF&& packet);
    bool state_ignore(GWBUF&& packet);

    bool ignore_reply(GWBUF&& packet, const mxs::ReplyRoute& down, const mxs::Reply& reply);
    bool checksum_reply(GWBUF&& packet, const mxs::ReplyRoute& down, const mxs::Reply& reply);
    bool compare_reply(GWBUF&& packet, const mxs::ReplyRoute& down, const mxs::Reply& reply);
    bool complete_reply(GWBUF&& packet, const mxs::ReplyRoute& down, const mxs::Reply& reply);
    bool discard_reply(GWBUF&& packet, const mxs::ReplyRoute& down, const mxs::Reply& reply);

    bool is_write(const GWBUF& packet) const;
    void track_query(const GWBUF& packet);
    bool rollback();
    void compute_checksum_from_reply(const mxs::Reply& reply);

    OptimisticTrx&            m_filter;
    State                     m_state {State::IDLE};
    mariadb::MultiPartTracker m_tracker;
    mariadb::TrxTracker       m_trx;

    mxb::small_vector<ReplyAction>      m_actions;
    mxb::xxHash                         m_hash;
    std::deque<mxb::xxHash::value_type> m_checksums;
    std::deque<GWBUF>                   m_packets;
};

class OptimisticTrx final : public mxs::Filter
{
public:
    OptimisticTrx(const OptimisticTrx&) = delete;
    OptimisticTrx& operator=(const OptimisticTrx&) = delete;

    static OptimisticTrx* create(const char* name)
    {
        return new OptimisticTrx(name);
    }

    std::shared_ptr<mxs::FilterSession> newSession(MXS_SESSION* pSession, SERVICE* pService) override
    {
        return std::make_shared<OptimisticTrxSession>(pSession, pService, *this);
    }

    json_t* diagnostics() const override;

    uint64_t getCapabilities() const override
    {
        return RCAP_TYPE_QUERY_CLASSIFICATION;
    }

    mxs::config::Configuration& getConfiguration() override
    {
        return m_config;
    }

    std::set<std::string> protocols() const override
    {
        return {MXS_MARIADB_PROTOCOL_NAME};
    }

    void success()
    {
        m_success.fetch_add(1, std::memory_order_relaxed);
    }

    void rollback()
    {
        m_rollback.fetch_add(1, std::memory_order_relaxed);
    }

private:
    OptimisticTrx(const std::string& name);

    mxs::config::Configuration m_config;
    std::atomic<int64_t>       m_success{0};
    std::atomic<int64_t>       m_rollback{0};
};
