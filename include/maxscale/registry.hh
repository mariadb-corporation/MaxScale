/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-01-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/ccdefs.hh>

namespace maxscale
{
/* Helper type for Registry. Must be specialized for each EntryType. The types
 * listed below are just examples and will not compile. */
template<typename EntryType>
struct RegistryTraits
{
    typedef int        id_type;
    typedef EntryType* entry_type;

    static id_type get_id(entry_type entry)
    {
        static_assert(sizeof(EntryType) != sizeof(EntryType),
                      "get_id() and the"
                      " surrounding struct must be specialized for every EntryType!");
        return 0;
    }
    static entry_type null_entry()
    {
        return NULL;
    }
};

/**
 * Class Registy wraps a map, allowing only a few operations on it. The intended
 * use is simple registries, such as the session registry in Worker. The owner
 * can expose a reference to this class without exposing all the methods the
 * underlying container implements. When instantiating with a new EntryType, the
 * traits-class RegistryTraits should be specialized for the new type as well.
 */
template<typename EntryType>
class Registry
{
    Registry(const Registry&);
    Registry& operator=(const Registry&);
public:
    typedef typename RegistryTraits<EntryType>::id_type      id_type;
    typedef typename RegistryTraits<EntryType>::entry_type   entry_type;
    typedef typename std::unordered_map<id_type, entry_type> ContainerType;
    typedef typename ContainerType::const_iterator           const_iterator;

    Registry()
    {
    }
    /**
     * Find an entry in the registry.
     *
     * @param id Entry key
     * @return The found entry, or NULL if not found
     */
    entry_type lookup(id_type id) const
    {
        entry_type rval = RegistryTraits<EntryType>::null_entry();
        typename ContainerType::const_iterator iter = m_registry.find(id);
        if (iter != m_registry.end())
        {
            rval = iter->second;
        }
        return rval;
    }

    /**
     * Add an entry to the registry.
     *
     * @param entry The entry to add
     * @return True if successful, false if id was already in
     */
    bool add(entry_type entry)
    {
        id_type id = RegistryTraits<EntryType>::get_id(entry);
        typename ContainerType::value_type new_value(id, entry);
        return m_registry.insert(new_value).second;
    }

    /**
     * Remove an entry from the registry.
     *
     * @param id Entry id
     * @return True if an entry was removed, false if not found
     */
    bool remove(id_type id)
    {
        entry_type rval = lookup(id);
        if (rval)
        {
            m_registry.erase(id);
        }
        return rval;
    }

    const_iterator begin() const
    {
        return m_registry.begin();
    }

    const_iterator end() const
    {
        return m_registry.end();
    }

    bool empty() const
    {
        return m_registry.empty();
    }

    auto size() const
    {
        return m_registry.size();
    }

private:
    ContainerType m_registry;
};
}
