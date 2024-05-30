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

#include "optimistictrx.hh"
#include <maxscale/protocol/mariadb/mariadbparser.hh>

namespace
{
mxs::config::Specification s_spec(MXB_MODULE_NAME, mxs::config::Specification::FILTER);

template<class T>
void hash_integer(mxb::xxHash& hash, T t)
{
    hash.update((const uint8_t*)&t, sizeof(t));
}

void hash_string(mxb::xxHash& hash, std::string_view str)
{
    hash.update((const uint8_t*)str.data(), str.size());
}
}

OptimisticTrxSession::OptimisticTrxSession(MXS_SESSION* pSession, SERVICE* pService, OptimisticTrx& filter)
    : mxs::FilterSession(pSession, pService)
    , m_filter(filter)
{
}

bool OptimisticTrxSession::routeQuery(GWBUF&& packet)
{
    track_query(packet);

    if (m_tracker.should_ignore() || !mxs_mysql_command_will_respond(mariadb::get_command(packet)))
    {
        return mxs::FilterSession::routeQuery(std::move(packet));
    }

    switch (m_state)
    {
    case State::IDLE:
        return state_idle(std::move(packet));

    case State::COLLECT:
        return state_collect(std::move(packet));

    case State::IGNORE:
        return state_ignore(std::move(packet));
    }

    mxb_assert(!true);
    return false;
}

bool OptimisticTrxSession::state_idle(GWBUF&& packet)
{
    if (m_trx.is_trx_starting())
    {
        if (parser().get_type_mask(packet) & mxs::sql::TYPE_BEGIN_TRX)
        {
            MXB_INFO("Starting optimistic transaction: %s", get_sql_string(packet).c_str());
            m_state = State::COLLECT;
            packet.set_type(GWBUF::TYPE_COLLECT_ROWS);
            m_packets.push_back(std::move(packet));

            m_actions.push_back(CHECKSUM);
            packet = mariadb::create_query("START TRANSACTION READ ONLY");
            packet.set_type(GWBUF::TYPE_COLLECT_ROWS);
        }
        else if (!is_write(packet))
        {
            MXB_INFO("Starting optimistic transaction (autocommit=0): %s", get_sql_string(packet).c_str());
            mxb_assert(!m_trx.is_autocommit());
            m_state = State::COLLECT;

            // If autocommit is disabled and this is a read that starts a transaction, the START TRANSACTION
            // READ ONLY must be injected into the query stream and the result of it must be discarded. Unlike
            // with explicit transactions (i.e. BEGIN), the two results are not comparable.
            m_actions.push_back(DISCARD);

            if (!mxs::FilterSession::routeQuery(mariadb::create_query("START TRANSACTION READ ONLY")))
            {
                return false;
            }

            m_actions.push_back(CHECKSUM);
            packet.set_type(GWBUF::TYPE_COLLECT_ROWS);
            m_packets.push_back(packet.shallow_clone());
        }
        else
        {
            MXB_INFO("Transaction starts with a write: %s", get_sql_string(packet).c_str());
            m_state = State::IGNORE;
            m_actions.push_back(IGNORE);
        }
    }
    else
    {
        MXB_INFO("Not collecting query: %s", get_sql_string(packet).c_str());
        m_actions.push_back(IGNORE);
    }

    return mxs::FilterSession::routeQuery(std::move(packet));
}


bool OptimisticTrxSession::state_collect(GWBUF&& packet)
{
    mxb_assert_message(m_trx.is_trx_active(), "The end of the transaction should be seen by this filter");

    if (m_trx.is_trx_ending())
    {
        MXB_INFO("Optimistic transaction complete");
        m_actions.push_back(COMPLETE);
        m_state = State::IDLE;
    }
    else if (is_write(packet))
    {
        MXB_INFO("Rolling back optimistic transaction: %s", get_sql_string(packet).c_str());

        if (!rollback())
        {
            MXB_ERROR("Rollback failed");
            return false;
        }

        m_actions.push_back(IGNORE);
        m_state = State::IGNORE;
    }
    else
    {
        MXB_INFO("Storing checksum of: %s", get_sql_string(packet).c_str());
        m_actions.push_back(CHECKSUM);
        packet.set_type(GWBUF::TYPE_COLLECT_ROWS);
        m_packets.push_back(packet.shallow_clone());
    }

    return mxs::FilterSession::routeQuery(std::move(packet));
}

bool OptimisticTrxSession::state_ignore(GWBUF&& packet)
{
    MXB_INFO("Ignoring query: %s", get_sql_string(packet).c_str());

    if (m_trx.is_trx_ending())
    {
        m_state = State::IDLE;
        m_actions.push_back(COMPLETE);
    }
    else
    {
        m_actions.push_back(IGNORE);
    }


    return mxs::FilterSession::routeQuery(std::move(packet));
}

bool OptimisticTrxSession::clientReply(GWBUF&& packet, const mxs::ReplyRoute& down, const mxs::Reply& reply)
{
    m_tracker.track_reply(reply);
    m_trx.fix_trx_state(reply);

    mxb_assert(!m_actions.empty());
    auto action = m_actions.front();

    if (reply.is_complete())
    {
        m_actions.erase(m_actions.begin());
    }

    switch (action)
    {
    case IGNORE:
        return ignore_reply(std::move(packet), down, reply);

    case CHECKSUM:
        return checksum_reply(std::move(packet), down, reply);

    case COMPARE:
        return compare_reply(std::move(packet), down, reply);

    case COMPLETE:
        return complete_reply(std::move(packet), down, reply);

    case DISCARD:
        return discard_reply(std::move(packet), down, reply);
    }

    mxb_assert(!true);
    return false;
}

bool OptimisticTrxSession::ignore_reply(GWBUF&& packet, const mxs::ReplyRoute& down, const mxs::Reply& reply)
{
    MXB_INFO("Ignoring: %s", reply.describe().c_str());
    return mxs::FilterSession::clientReply(std::move(packet), down, reply);
}

bool OptimisticTrxSession::discard_reply(GWBUF&& packet, const mxs::ReplyRoute& down, const mxs::Reply& reply)
{
    MXB_INFO("Discarding: %s", reply.describe().c_str());
    return true;
}

bool OptimisticTrxSession::checksum_reply(GWBUF&& packet, const mxs::ReplyRoute& down,
                                          const mxs::Reply& reply)
{
    compute_checksum_from_reply(reply);

    if (reply.is_complete())
    {
        MXB_INFO("Storing checksum, %s", reply.describe().c_str());
        m_checksums.push_back(m_hash.value());
        m_hash.reset();
    }

    return mxs::FilterSession::clientReply(std::move(packet), down, reply);
}

bool OptimisticTrxSession::compare_reply(GWBUF&& packet, const mxs::ReplyRoute& down, const mxs::Reply& reply)
{
    compute_checksum_from_reply(reply);

    if (reply.is_complete())
    {
        mxb_assert(!m_checksums.empty());
        MXB_INFO("Comparing, checksum %s: %s", m_checksums.front() != m_hash.value() ? "mismatch" : "match",
                 reply.describe().c_str());

        if (m_checksums.front() != m_hash.value())
        {
            return false;
        }

        mxb_assert(m_packets.size() >= m_checksums.size());
        m_checksums.pop_front();
        m_packets.pop_front();
        m_hash.reset();
    }

    return true;
}

bool OptimisticTrxSession::complete_reply(GWBUF&& packet, const mxs::ReplyRoute& down,
                                          const mxs::Reply& reply)
{
    if (reply.is_complete())
    {
        // If the transaction was rolled back, then the checksums and packets are consumed during the checksum
        // verification process. If the transaction completed successfully, the checksums can all be cleared
        // but the packet container may have more packets in it that belong to other transactions.
        mxb_assert(m_packets.size() >= m_checksums.size());
        m_packets.erase(m_packets.begin(), m_packets.begin() + m_checksums.size());
        m_checksums.clear();
    }

    return mxs::FilterSession::clientReply(std::move(packet), down, reply);
}

bool OptimisticTrxSession::is_write(const GWBUF& packet) const
{
    uint32_t type = parser().get_type_mask(packet);
    const uint32_t mask = mxs::sql::TYPE_READ | mxs::sql::TYPE_USERVAR_READ
        | mxs::sql::TYPE_SYSVAR_READ | mxs::sql::TYPE_GSYSVAR_READ;

    return type & ~mask;
}

void OptimisticTrxSession::track_query(const GWBUF& packet)
{
    m_tracker.track_query(packet);

    if (!m_tracker.should_ignore())
    {
        m_trx.track_transaction_state(packet, MariaDBParser::get());
    }
}

bool OptimisticTrxSession::rollback()
{
    if (!mxs::FilterSession::routeQuery(mariadb::create_query("ROLLBACK")))
    {
        return false;
    }

    m_actions.push_back(DISCARD);

    for (auto&& packet : m_packets)
    {
        if (!mxs::FilterSession::routeQuery(std::move(packet)))
        {
            return false;
        }

        m_actions.push_back(COMPARE);
    }

    m_filter.rollback();
    return true;
}


void OptimisticTrxSession::compute_checksum_from_reply(const mxs::Reply& reply)
{
    // The checksum computation must ignore the status field in OK and EOF packets. As the optimistic
    // transaction is started with a START TRANSACTION READ ONLY, the SERVER_STATUS_IN_TRANS_READONLY bit is
    // always set in the optimistic transaction but is never set in the original one. Thus a checksum of the
    // raw data will never match.
    for (const auto& row : reply.row_data())
    {
        for (const auto& col : row)
        {
            hash_string(m_hash, col);
        }
    }

    if (reply.is_ok())
    {
        hash_integer(m_hash, reply.affected_rows());
        hash_integer(m_hash, reply.last_insert_id());
        hash_integer(m_hash, reply.num_warnings());
    }
    else if (reply.error())
    {
        hash_integer(m_hash, reply.error().code());
        hash_string(m_hash, reply.error().sql_state());
        hash_string(m_hash, reply.error().message());
    }

    if (reply.is_complete())
    {
        m_hash.finalize();
    }
}

OptimisticTrx::OptimisticTrx(const std::string& name)
    : m_config(name, &s_spec)
{
}

json_t* OptimisticTrx::diagnostics() const
{
    json_t* js = json_object();
    json_object_set_new(js, "success", json_integer(m_success.load(std::memory_order_relaxed)));
    json_object_set_new(js, "rollback", json_integer(m_rollback.load(std::memory_order_relaxed)));
    return js;
}

extern "C" MXB_API MXS_MODULE* MXS_CREATE_MODULE()
{
    static MXS_MODULE info =
    {
        mxs::MODULE_INFO_VERSION,
        MXB_MODULE_NAME,
        mxs::ModuleType::FILTER,
        mxs::ModuleStatus::GA,
        MXS_FILTER_VERSION,
        "Optimistic transaction execution filter",
        "V1.0.0",
        MXS_NO_MODULE_CAPABILITIES,
        &mxs::FilterApi<OptimisticTrx>::s_api,
        nullptr,    /* Process init. */
        nullptr,    /* Process finish. */
        nullptr,    /* Thread init. */
        nullptr,    /* Thread finish. */
        &s_spec
    };

    return &info;
}
