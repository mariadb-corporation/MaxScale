/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-04-10
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
    struct Entry
    {
        void set(void* data, void (*deleter)(void*), size_t (*sizer)(void*))
        {
            this->data = data;
            this->deleter = deleter;
            this->sizer = sizer;
        }

        void reset()
        {
            set(nullptr, nullptr, nullptr);
        }

        void*    data { nullptr };
        void   (*deleter)(void*) { nullptr };
        size_t (*sizer)(void*) { nullptr };
    };

    using Entries = std::vector<Entry>;

    IndexedStorage() = default;
    IndexedStorage(const IndexedStorage&) = delete;
    IndexedStorage& operator=(const IndexedStorage&) = delete;

    ~IndexedStorage();

    /**
     * Removes all stored values.
     *
     * @return An estimate of the size of the deleted memory.
     */
    size_t clear();

    /**
     * Initialize a globally unique data identifier
     *
     * @return The data identifier usable for indexed local data storage
     */
    static uint64_t create_key();

    /**
     * Set local data
     *
     * @param key      Key acquired with create_local_data
     * @param data     Data to store
     * @param deleter  Function for deleting @c data
     * @param sizer    Function for obtaining the size of @c data
     */
    void set_data(uint64_t key, void* data, void (* deleter)(void*), size_t (* sizer)(void*))
    {
        if (m_entries.size() <= key)
        {
            m_entries.resize(key + 1);
        }

        m_entries[key].set(data, deleter, sizer);
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
        return key < m_entries.size() ? m_entries[key].data : nullptr;
    }

    /**
     * Deletes local data
     *
     * If a callback was passed when the data was set, it will be called.
     *
     * @param key Key to remove
     *
     * @return An estimate of the amount of data that was released.
     */
    size_t delete_data(uint64_t key)
    {
        size_t rv = 0;
        if (key < m_entries.size())
        {
            Entry& entry = m_entries[key];

            if (entry.sizer)
            {
                rv += entry.sizer(entry.data);
            }

            if (entry.deleter)
            {
                entry.deleter(entry.data);
            }

            entry.reset();
        }

        return rv;
    }

private:
    Entries m_entries;
};
}
