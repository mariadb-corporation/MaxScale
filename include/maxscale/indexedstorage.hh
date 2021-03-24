/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-03-24
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/ccdefs.hh>
#include <atomic>
#include <vector>

namespace maxscale
{

/**
 * @class IndexedStorage
 *
 * An instance of this class can hold data, indexed using an integer.
 *
 * Whoever wants to store data in this storage, should first call
 * @c create_key() to get a unique index. Thereafter that key can
 * be used for setting, getting and deleting the data.
 *
 * Note that although there can be numerous instances of
 * IndexedStorage, they will all share the same key generator. The
 * primary purpose of IndexedStorage is to store the "same" data
 * uniquely in different threads.
 */
class IndexedStorage
{
public:
    using LocalData = std::vector<void*>;
    using DataDeleters = std::vector<void (*)(void*)>;

    IndexedStorage() = default;
    IndexedStorage(const IndexedStorage&) = delete;
    IndexedStorage& operator=(const IndexedStorage&) = delete;

    ~IndexedStorage();

    /**
     * Initialize a globally unique data identifier
     *
     * @return The data identifier usable for indexed local data storage
     */
    static uint64_t create_key()
    {
        static std::atomic<uint64_t> id_generator {0};
        return id_generator.fetch_add(1, std::memory_order_relaxed);
    }

    /**
     * Set local data
     *
     * @param key  Key acquired with create_local_data
     * @param data Data to store
     */
    void set_data(uint64_t key, void* data, void (* callback)(void*))
    {
        if (m_local_data.size() <= key)
        {
            m_local_data.resize(key + 1, nullptr);
            m_data_deleters.resize(key + 1, nullptr);
        }

        if (callback)
        {
            m_data_deleters[key] = callback;
        }

        m_local_data[key] = data;
    }

    /**
     * Get local data
     *
     * @param key Key to use
     *
     * @return Data previously stored
     */
    void* get_data(uint64_t key) const
    {
        return key < m_local_data.size() ? m_local_data[key] : nullptr;
    }

    /**
     * Deletes local data
     *
     * If a callback was passed when the data was set, it will be called.
     *
     * @param key Key to remove
     */
    void delete_data(uint64_t key)
    {
        if (key < m_local_data.size())
        {
            if (auto deleter = m_data_deleters[key])
            {
                deleter(m_local_data[key]);
            }

            m_data_deleters[key] = nullptr;
            m_local_data[key] = nullptr;
        }
    }

private:
    LocalData    m_local_data;
    DataDeleters m_data_deleters;
};

}
