/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-04-28
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/ccdefs.hh>
#include <unordered_map>
#include "cachefilter.hh"
#include "cache_storage_api.hh"
#include "storage.hh"

class LRUStorage : public Storage
{
public:
    ~LRUStorage();

    /**
     * @see Storage::create_token
     *
     * @return Always NULL.
     */
    bool create_token(std::shared_ptr<Token>* psToken) override final;

    /**
     * @see Storage::get_config
     */
    void get_config(Config* pConfig) override final;

    /**
     * @see Storage::get_limits
     */
    void get_limits(Limits* pLimits) override final;

protected:
    LRUStorage(const Config& config, Storage* pStorage);

    /**
     * @see Storage::get_info
     */
    cache_result_t do_get_info(uint32_t what, json_t** ppInfo) const;

    /**
     * @see Storage::get_value
     */
    cache_result_t do_get_value(Token* pToken,
                                const CacheKey& key,
                                uint32_t flags,
                                uint32_t soft_ttl,
                                uint32_t hard_ttl,
                                GWBUF** ppValue);

    /**
     * @see Storage::put_value
     */
    cache_result_t do_put_value(Token* pToken,
                                const CacheKey& key,
                                const std::vector<std::string>& invalidation_words,
                                const GWBUF* pValue);

    /**
     * @see Storage::del_value
     */
    cache_result_t do_del_value(Token* pToken,
                                const CacheKey& key);

    /**
     * @see Storage::invalidate
     */
    cache_result_t do_invalidate(Token* pToken,
                                 const std::vector<std::string>& words);

    /**
     * @see Storage::clear
     */
    cache_result_t do_clear(Token* pToken);

    /**
     * @see Storage::get_head
     */
    cache_result_t do_get_head(CacheKey* pKey,
                               GWBUF** ppValue);

    /**
     * @see Storage::get_tail
     */
    cache_result_t do_get_tail(CacheKey* pKey,
                               GWBUF** ppValue);

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
    LRUStorage& operator=(const LRUStorage&);

    enum access_approach_t
    {
        APPROACH_GET,   // Update head
        APPROACH_PEEK   // Do not update head
    };

    cache_result_t access_value(access_approach_t approach,
                                const CacheKey& key,
                                uint32_t flags,
                                uint32_t soft_ttl,
                                uint32_t hard_ttl,
                                GWBUF** ppValue);

    cache_result_t peek_value(const CacheKey& key,
                              uint32_t flags,
                              GWBUF** ppValue)
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
        {
        }
        ~Node()
        {
            remove();
        }

        const CacheKey* key() const
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

        const std::vector<std::string>& invalidation_words() const
        {
            return m_invalidation_words;
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

        void reset(const CacheKey* pKey,
                   size_t size,
                   const std::vector<std::string>& invalidation_words)
        {
            m_pKey = pKey;
            m_size = size;
            m_invalidation_words = invalidation_words;
        }

        void clear()
        {
            m_pKey = nullptr;
            m_size = 0;
            m_invalidation_words.clear();
        }

    private:
        // TODO: Replace string with char* that points to a shared string.
        // TODO: No sense in storing the same table name a million times.
        using Words = std::vector<std::string>;

        const CacheKey* m_pKey;               /*< Points at the key stored in nodes_by_key below. */
        size_t           m_size;               /*< The size of the data referred to by m_pKey. */
        Node*            m_pNext;              /*< The next node in the LRU list. */
        Node*            m_pPrev;              /*< The previous node in the LRU list. */
        Words            m_invalidation_words; /*< Words that invalidate this node. */
    };

    typedef std::unordered_map<CacheKey, Node*> NodesByKey;

    enum class Context
    {
        EVICTION,        /*< Evict (aka free) LRU node and cache value. */
        INVALIDATION,    /*< Invalidate (aka free) LRU node and cache value. */
        LRU_INVALIDATION /*< Invalidate (aka free) LRU node, but leave cache value. */
    };

    Node* vacate_lru();
    Node* vacate_lru(size_t space);
    bool  free_node_data(Node* pNode, Context context);
    void  free_node(Node* pNode) const;
    void  free_node(NodesByKey::iterator& i) const;
    void  remove_node(Node* pNode) const;
    void  move_to_head(Node* pNode) const;

    cache_result_t get_existing_node(NodesByKey::iterator& i, const GWBUF* pvalue, Node** ppNode);
    cache_result_t get_new_node(const CacheKey& key,
                                const GWBUF* pValue,
                                NodesByKey::iterator* pI,
                                Node** ppNode);

    bool invalidate(Node* pNode, Context context);

    class Invalidator;
    class NullInvalidator;
    class LRUInvalidator;
    class FullInvalidator;
    class StorageInvalidator;

    Storage* storage() const
    {
        return m_pStorage;
    }

private:
    struct Stats
    {
        void fill(json_t* pObject) const;

        uint64_t size = 0;          /*< The total size of the stored values. */
        uint64_t items = 0;         /*< The number of stored items. */
        uint64_t hits = 0;          /*< How many times a key was found in the cache. */
        uint64_t misses = 0;        /*< How many times a key was not found in the cache. */
        uint64_t updates = 0;       /*< How many times an existing key in the cache was updated. */
        uint64_t deletes = 0;       /*< How many times an existing key in the cache was deleted. */
        uint64_t evictions = 0;     /*< How many times an item has been evicted from the cache. */
        uint64_t invalidations = 0; /*< How many times an item has been invalidated. */
    };

    using SInvalidator = std::unique_ptr<Invalidator>;

    const Config         m_config;        /*< The configuration. */
    Storage*             m_pStorage;      /*< The actual storage. */
    const uint64_t       m_max_count;     /*< The maximum number of items in the LRU list, */
    const uint64_t       m_max_size;      /*< The maximum size of all cached items. */
    mutable Stats        m_stats;         /*< Cache statistics. */
    mutable NodesByKey   m_nodes_by_key;  /*< Mapping from cache keys to corresponding Node. */
    mutable Node*        m_pHead;         /*< The node at the LRU list. */
    mutable Node*        m_pTail;         /*< The node at bottom of the LRU list.*/
    mutable SInvalidator m_sInvalidator;  /*< The invalidator. */
};
