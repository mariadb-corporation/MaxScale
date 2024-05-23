/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-05-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxbase/ccdefs.hh>
#include <maxbase/assert.hh>
#include <unordered_map>
#include <list>
#include <functional>
#include <type_traits>

namespace maxbase
{
/**
 * Simple LRU cache with manual eviction
 *
 * The LRU cache is intended to be a simple LRU container on top of which custom LRU caches can be built that
 * take the object size into account. The container should be a mostly-drop-in replacement for simple uses of
 * std::unordered_map in MaxScale.
 *
 * @tparam Key    The cache key type.
 * @tparam Value  The cache value type.
 * @tparam Hash   The hash to use, std::hash<Key> by default.
 * @tparam Equals The equality comparison to use, std::equal_to<Key> by default.
 */
template<typename Key, typename Value, typename Hash = std::hash<Key>, typename Equal = std::equal_to<Key>>
class lru_cache
{
public:
    // The keys in the hashtable store a reference to the actual key that's stored in the LRU list. This
    // avoids having a copy of the key in both the hashtable and the list. The result of this is that the size
    // overhead per entry is constant.
    using KeyRef = std::reference_wrapper<std::add_const_t<Key>>;

    struct ref_equal_to
    {
        bool operator()(const KeyRef& a, const KeyRef& b) const
        {
            return Equal{}(a.get(), b.get());
        }
    };

    struct ref_hash
    {
        size_t operator()(const KeyRef& k) const
        {
            return Hash{}(k.get());
        }
    };

    using value_type = std::pair<std::add_const_t<Key>, Value>;
    using LRUList = std::list<value_type>;
    using HashTable = std::unordered_map<KeyRef, typename LRUList::iterator, ref_hash, ref_equal_to>;
    using reference = typename LRUList::reference;
    using const_reference = typename LRUList::const_reference;
    using iterator = typename LRUList::iterator;
    using const_iterator = typename LRUList::const_iterator;

    // This is a rough estimate of how much extra space is actually needed to store one entry. These are very
    // rough estimates and they assume that e.g. std::list is implemented using two pointers per node. The
    // "hidden" overhead should be around 64 bytes on your average Linux system.
    static constexpr size_t ENTRY_HIDDEN_OVERHEAD =
        sizeof(void*) * 2                       // The two pointers in the std::list nodes.
        + sizeof(typename HashTable::value_type)// The hashtable values, the KeyRef and the iterator
        + sizeof(void*) * 4;                    // The estimated std::unordered_map overhead.

    /**
     * Find a value in the cache
     *
     * This will update the LRU list and modify the iteration order over the keys. Never call this function
     * while iterating over the container, otherwise this will end up with an infinite loop as the values get
     * moved at the head of the list.
     *
     * @param key The key to look for
     *
     * @return Iterator to the value or the end iterator if it was not found
     */
    iterator find(const Key& key)
    {
        return find_by_key(key);
    }

    const_iterator find(const Key& key) const
    {
        return const_cast<lru_cache<Key, Value, Hash>*>(this)->find_by_key(key);
    }

    /**
     * Find a value in the cache without updating the LRU list
     *
     * Unlike the find() function, this can be called while iterating over the container.
     *
     * @param key The key to look for
     *
     * @return Iterator to the value or the end iterator if it was not found
     */
    iterator peek(const Key& key)
    {
        auto it = m_hash.find(key);
        return it != m_hash.end() ? it->second : m_lru.end();
    }

    const_iterator peek(const Key& key) const
    {
        auto it = m_hash.find(key);
        return it != m_hash.end() ? it->second : m_lru.end();
    }

    /**
     * Insert a new entry into the cache
     *
     * @param val The entry to insert
     *
     * @return std::pair<iterator, bool> with the boolean set to true and the iterator to the inserted element
     *         if the value was inserted. If the value was not inserted because it already existed, the
     *         boolean is set to false and the iterator points to the existing value in the cache.
     */
    std::pair<iterator, bool> insert(value_type&& val)
    {
        return emplace_value(std::move(val));
    }

    /**
     * Emplace a new entry into the cache
     *
     * @param args Arguments passed to the std::pair constructor
     *
     * @return std::pair<iterator, bool> with the boolean set to true and the iterator to the inserted element
     *         if the value was inserted. If the value was not inserted because it already existed, the
     *         boolean is set to false and the iterator points to the existing value in the cache.
     */
    template<class ... Args>
    std::pair<iterator, bool> emplace(Args ... args)
    {
        return emplace_value(std::forward<Args>(args)...);
    }

    /**
     * Erase a value from the cache
     *
     * Invalidates iterators that point to the value that is erased.
     *
     * @param key The key of the value to erase
     */
    void erase(const Key& key)
    {
        if (auto it = m_hash.find(key); it != m_hash.end())
        {
            mxb_assert_message(std::distance(begin(), it->second) < (ptrdiff_t)size(),
                               "Iterator should be reachable from begin()");
            m_lru.erase(it->second);
            m_hash.erase(it);
        }
    }

    /**
     * Erase a value from the cache
     *
     * Invalidates iterators that point to the value that is erased.
     *
     * @param it Iterator to the value to be erased
     */
    void erase(iterator it)
    {
        mxb_assert_message(m_hash.find(it->first) != m_hash.end(),
                           "Hashtable should contain this iterator");
        m_hash.erase(it->first);
        m_lru.erase(it);
    }

    void erase(const_iterator it)
    {
        m_hash.erase(it->first);
        m_lru.erase(it);
    }

    /**
     * Get a reference to the least recently used value
     *
     * @return Reference to the least recently used value
     */
    reference back()
    {
        return m_lru.back();
    }

    const_reference back() const
    {
        return m_lru.back();
    }

    /**
     * Remove the least recently used value from the cache
     *
     * Invalidates iterators that point to the value that is erased.
     */
    void pop_back()
    {
        m_hash.erase(m_lru.back().first);
        m_lru.pop_back();
    }

    /**
     * Get a reference to the most recently used value
     *
     * @return Reference to the most recently used value
     */
    reference front()
    {
        return m_lru.front();
    }

    const_reference front() const
    {
        return m_lru.front();
    }

    /**
     * Remove the most recently used value from the cache
     *
     * Invalidates iterators that point to the value that is erased.
     */
    void pop_front()
    {
        m_hash.erase(m_lru.front().first);
        m_lru.pop_front();
    }

    /**
     * Get the first iterator of the range
     *
     * Never call find() while iterating over the container. The iteration order will be modified by the
     * find() call and with two or more values, the iteration would end up being circular. If you need to look
     * into the cache while iterating, use peek().
     *
     * @return The first iterator of the range
     */
    const_iterator begin() const
    {
        return m_lru.begin();
    }

    iterator begin()
    {
        return m_lru.begin();
    }

    /**
     * Get the past-the-end iterator of the range
     *
     * @return The past-the-end iterator of the range
     */
    const_iterator end() const
    {
        return m_lru.end();
    }

    iterator end()
    {
        return m_lru.end();
    }

    /**
     * Get the number of entries in the cache
     *
     * @return The number of entries in the cache
     */
    size_t size() const
    {
        mxb_assert(m_lru.size() == m_hash.size());
        return m_hash.size();
    }

    /**
     * Is the cache empty?
     *
     * @return True if the cache is empty
     */
    bool empty() const
    {
        mxb_assert(m_lru.size() == m_hash.size());
        return m_hash.empty();
    }

    /**
     * Clear the cache
     */
    void clear()
    {
        m_hash.clear();
        m_lru.clear();
    }

private:
    iterator find_by_key(const Key& key)
    {
        if (auto it = m_hash.find(key); it != m_hash.end())
        {
            m_lru.splice(m_lru.begin(), m_lru, it->second);
            return m_lru.begin();
        }

        return m_lru.end();
    }

    template<class ... Args>
    std::pair<iterator, bool> emplace_value(Args ... args)
    {
        const Key& key = m_lru.emplace_front(std::forward<Args>(args)...).first;

        if (auto [it, inserted] = m_hash.emplace(key, m_lru.begin()); !inserted)
        {
            m_lru.pop_front();
            return {it->second, false};
        }

        return {m_lru.begin(), true};
    }

    mutable LRUList m_lru;
    HashTable       m_hash;
};
}
