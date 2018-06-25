#pragma once
/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2022-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/cppdefs.hh>
#include <tr1/unordered_map>
#include "cachefilter.h"
#include "cache_storage_api.hh"
#include "storage.hh"

class LRUStorage : public Storage
{
public:
    ~LRUStorage();

    /**
     * @see Storage::get_config
     */
    void get_config(CACHE_STORAGE_CONFIG* pConfig);

protected:
    LRUStorage(const CACHE_STORAGE_CONFIG& config, Storage* pStorage);

    /**
     * @see Storage::get_info
     */
    cache_result_t do_get_info(uint32_t what, json_t** ppInfo) const;

    /**
     * @see Storage::get_value
     */
    cache_result_t do_get_value(const CACHE_KEY& key,
                                uint32_t flags,
                                uint32_t soft_ttl,
                                uint32_t hard_ttl,
                                GWBUF** ppValue) const;

    /**
     * @see Storage::put_value
     */
    cache_result_t do_put_value(const CACHE_KEY& key,
                                const GWBUF* pValue);

    /**
     * @see Storage::del_value
     */
    cache_result_t do_del_value(const CACHE_KEY& key);

    /**
     * @see Storage::get_head
     */
    cache_result_t do_get_head(CACHE_KEY* pKey,
                               GWBUF** ppValue) const;

    /**
     * @see Storage::get_tail
     */
    cache_result_t do_get_tail(CACHE_KEY* pKey,
                               GWBUF** ppValue) const;

    /**
     * @see Storage::getSize
     */
    cache_result_t do_get_size(uint64_t* pSize) const;

    /**
     * @see Storage::getItems
     */
    cache_result_t do_get_items(uint64_t* pItems) const;

private:
    LRUStorage(const LRUStorage&);
    LRUStorage& operator = (const LRUStorage&);

    enum access_approach_t
    {
        APPROACH_GET,  // Update head
        APPROACH_PEEK  // Do not update head
    };

    cache_result_t access_value(access_approach_t approach,
                                const CACHE_KEY& key,
                                uint32_t flags,
                                uint32_t soft_ttl,
                                uint32_t hard_ttl,
                                GWBUF** ppValue) const;

    cache_result_t peek_value(const CACHE_KEY& key,
                              uint32_t flags,
                              GWBUF** ppValue) const
    {
        return access_value(APPROACH_PEEK, key, flags, CACHE_USE_CONFIG_TTL, CACHE_USE_CONFIG_TTL, ppValue);
    }

    /**
     * The Node class is used for maintaining LRU information.
     */
    class Node
    {
    public:
        Node()
            : m_pKey(NULL)
            , m_size(0)
            , m_pNext(NULL)
            , m_pPrev(NULL)
        {}
        ~Node()
        {
            remove();
        }

        const CACHE_KEY* key() const
        {
            return m_pKey;
        }
        size_t size() const
        {
            return m_size;
        }
        Node* next() const
        {
            return m_pNext;
        }
        Node* prev() const
        {
            return m_pPrev;
        }

        /**
         * Move the node before the node provided as argument.
         *
         * @param  pnode  The node in front of which this should be moved.
         * @return This node.
         */
        Node* prepend(Node* pNode)
        {
            if (pNode && (pNode != this))
            {
                if (m_pPrev)
                {
                    m_pPrev->m_pNext = m_pNext;
                }

                if (m_pNext)
                {
                    m_pNext->m_pPrev = m_pPrev;
                }

                if (pNode->m_pPrev)
                {
                    pNode->m_pPrev->m_pNext = this;
                }

                m_pPrev = pNode->m_pPrev;
                m_pNext = pNode;

                pNode->m_pPrev = this;
            }

            return this;
        }

        /**
         * Remove this node from the list.
         *
         * @return The previous node if there is one, or the next node.
         */
        Node* remove()
        {
            if (m_pPrev)
            {
                m_pPrev->m_pNext = m_pNext;
            }

            if (m_pNext)
            {
                m_pNext->m_pPrev = m_pPrev;
            }

            Node* pNode = (m_pPrev ? m_pPrev : m_pNext);

            m_pPrev = NULL;
            m_pNext = NULL;

            return pNode;
        }

        void reset(const CACHE_KEY* pkey = NULL, size_t size = 0)
        {
            m_pKey = pkey;
            m_size = size;
        }

    private:
        const CACHE_KEY* m_pKey;  /*< Points at the key stored in nodes_by_key_ below. */
        size_t           m_size;  /*< The size of the data referred to by m_pKey. */
        Node*            m_pNext; /*< The next node in the LRU list. */
        Node*            m_pPrev; /*< The previous node in the LRU list. */
    };

    typedef std::tr1::unordered_map<CACHE_KEY, Node*> NodesByKey;

    Node* vacate_lru();
    Node* vacate_lru(size_t space);
    bool free_node_data(Node* pNode);
    void free_node(Node* pNode) const;
    void free_node(NodesByKey::iterator& i) const;
    void remove_node(Node* pNode) const;
    void move_to_head(Node* pNode) const;

    cache_result_t get_existing_node(NodesByKey::iterator& i, const GWBUF* pvalue, Node** ppNode);
    cache_result_t get_new_node(const CACHE_KEY& key,
                                const GWBUF* pValue,
                                NodesByKey::iterator* pI,
                                Node** ppNode);

private:
    struct Stats
    {
        Stats()
            : size(0)
            , items(0)
            , hits(0)
            , misses(0)
            , updates(0)
            , deletes(0)
            , evictions(0)
        {}

        void fill(json_t* pObject) const;

        uint64_t size;       /*< The total size of the stored values. */
        uint64_t items;      /*< The number of stored items. */
        uint64_t hits;       /*< How many times a key was found in the cache. */
        uint64_t misses;     /*< How many times a key was not found in the cache. */
        uint64_t updates;    /*< How many times an existing key in the cache was updated. */
        uint64_t deletes;    /*< How many times an existing key in the cache was deleted. */
        uint64_t evictions;  /*< How many times an item has been evicted from the cache. */
    };

    const CACHE_STORAGE_CONFIG m_config;       /*< The configuration. */
    Storage*                   m_pStorage;     /*< The actual storage. */
    const uint64_t             m_max_count;    /*< The maximum number of items in the LRU list, */
    const uint64_t             m_max_size;     /*< The maximum size of all cached items. */
    mutable Stats              m_stats;        /*< Cache statistics. */
    mutable NodesByKey         m_nodes_by_key; /*< Mapping from cache keys to corresponding Node. */
    mutable Node*              m_pHead;        /*< The node at the LRU list. */
    mutable Node*              m_pTail;        /*< The node at bottom of the LRU list.*/
};
