/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/indexedstorage.hh>

namespace
{
// It's important that this variable is defined only once and that access to it is not inlined. Previously
// when it was stored as a function scope static variable, a module linked with -fvisibility=hidden would end
// up having its own version of this variable. This would cause multiple WorkerLocal instances to use the same
// key which in turn caused two different types to share the same storage and the objects themselves would
// appear corrupted.
std::atomic<uint64_t> id_generator {0};
}

namespace maxscale
{

// static
uint64_t IndexedStorage::create_key()
{
    return id_generator.fetch_add(1, std::memory_order_relaxed);
}

size_t IndexedStorage::clear()
{
    size_t rv = 0;

    for (uint64_t key = 0; key < m_entries.size(); ++key)
    {
        Entry& entry = m_entries[key];

        if (entry.data)
        {
            if (entry.sizer)
            {
                rv += entry.sizer(entry.data);
            }

            if (entry.deleter)
            {
                entry.deleter(entry.data);
            }
        }
    }

    // m_entries.clear() would not actually delete the memory.
    Entries().swap(m_entries);

    return rv;
}

IndexedStorage::~IndexedStorage()
{
    clear();
}
}
