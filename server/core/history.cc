/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-02-21
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/history.hh>
#include <maxscale/service.hh>
#include <utility>

namespace maxscale
{
History::History(size_t limit)
    : m_max_sescmd_history(limit)
{
}

void History::add(GWBUF&& buffer, bool ok)
{
    m_history_responses.emplace(buffer.id(), ok);
    m_history.emplace_back(std::move(buffer));

    if (m_history.size() > m_max_sescmd_history)
    {
        prune_history();
    }
}

bool History::erase(uint32_t id)
{
    bool erased = false;
    auto it = std::remove_if(m_history.begin(), m_history.end(), [&](const auto& buf){
        return buf.id() == id;
    });

    if (it != m_history.end())
    {
        m_history.erase(it, m_history.end());
        erased = true;
    }

    return erased;
}

void History::prune_history()
{
    // Using the about-to-be-pruned command as the minimum ID prevents the removal of responses that are still
    // needed when the ID overflows. If only the stored positions were used, the whole history would be
    // cleared.
    uint32_t min_id = m_history.front().id();

    for (const auto& kv : m_history_info)
    {
        if (kv.second.position > 0 && kv.second.position < min_id)
        {
            min_id = kv.second.position;
        }
    }

    m_history_responses.erase(m_history_responses.begin(), m_history_responses.lower_bound(min_id));
    m_history.pop_front();
    m_history_pruned = true;
}

void History::pin_responses(mxs::BackendConnection* backend)
{
    if (!m_history.empty())
    {
        m_history_info[backend].position = m_history.front().id();
    }
}

void History::set_position(mxs::BackendConnection* backend, uint32_t position)
{
    m_history_info[backend].position = position;
}

void History::need_response(mxs::BackendConnection* backend, std::function<void()> fn)
{
    m_history_info[backend].waiting_for_response = true;
    m_history_info[backend].response_cb = std::move(fn);
}

void History::remove(mxs::BackendConnection* backend)
{
    m_history_info.erase(backend);
}

std::optional<bool> History::get(uint32_t id) const
{
    std::optional<bool> rval;

    if (auto it = m_history_responses.find(id); it != m_history_responses.end())
    {
        rval = it->second;
    }

    return rval;
}

void History::check_early_responses()
{
    // Call check_history() for any backends that responded before the accepted response was received.
    // Collect the backends first into a separate vector: the backend might end up removing itself from the
    // history which will modify m_history_info while we're iterating over it.
    std::vector<std::function<void()>> backends;

    for (auto& [backend, info] : m_history_info)
    {
        if (std::exchange(info.waiting_for_response, false))
        {
            backends.push_back(info.response_cb);
        }
    }

    for (auto fn : backends)
    {
        fn();
    }
}

size_t History::runtime_size() const
{
    size_t sescmd_history = 0;

    for (const GWBUF& buf : m_history)
    {
        sescmd_history += buf.runtime_size();
    }

    // The map overhead is ignored.
    sescmd_history += m_history_responses.size() * sizeof(decltype(m_history_responses)::value_type);
    sescmd_history += m_history_info.size() * sizeof(decltype(m_history_info)::value_type);

    return sescmd_history;
}
}
