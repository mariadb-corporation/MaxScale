/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-03-20
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/indexedstorage.hh>

namespace maxscale
{

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
