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

#define MXS_MODULE_NAME "cache"
#include "lrustorage.hh"

LRUStorage::LRUStorage(const CACHE_STORAGE_CONFIG& config, Storage* pStorage)
    : m_config(config)
    , m_pStorage(pStorage)
    , m_max_count(config.max_count != 0 ? config.max_count : UINT64_MAX)
    , m_max_size(config.max_size != 0 ? config.max_size : UINT64_MAX)
    , m_pHead(NULL)
    , m_pTail(NULL)
{
}

LRUStorage::~LRUStorage()
{
    Node* pnode = m_pHead;

    while (m_pHead)
    {
        free_node(m_pHead); // Adjusts m_pHead
    }

    delete m_pStorage;
}

void LRUStorage::get_config(CACHE_STORAGE_CONFIG* pConfig)
{
    *pConfig = m_config;
}

cache_result_t LRUStorage::do_get_info(uint32_t what,
                                       json_t** ppInfo) const
{
    *ppInfo = json_object();

    if (*ppInfo)
    {
        json_t* pLru = json_object();

        if (pLru)
        {
            m_stats.fill(pLru);

            json_object_set(*ppInfo, "lru", pLru);
            json_decref(pLru);
        }

        json_t* pStorage_info;

        cache_result_t result = m_pStorage->get_info(what, &pStorage_info);

        if (CACHE_RESULT_IS_OK(result))
        {
            json_object_set(*ppInfo, "real_storage", pStorage_info);
            json_decref(pStorage_info);
        }
    }

    return *ppInfo ? CACHE_RESULT_OK : CACHE_RESULT_OUT_OF_RESOURCES;
}

cache_result_t LRUStorage::do_get_value(const CACHE_KEY& key,
                                        uint32_t flags,
                                        uint32_t soft_ttl,
                                        uint32_t hard_ttl,
                                        GWBUF** ppValue) const
{
    return access_value(APPROACH_GET, key, flags, soft_ttl, hard_ttl, ppValue);
}

cache_result_t LRUStorage::do_put_value(const CACHE_KEY& key, const GWBUF* pvalue)
{
    cache_result_t result = CACHE_RESULT_ERROR;

    size_t value_size = GWBUF_LENGTH(pvalue);

    Node* pNode = NULL;

    NodesByKey::iterator i = m_nodes_by_key.find(key);
    bool existed = (i != m_nodes_by_key.end());

    if (existed)
    {
        result = get_existing_node(i, pvalue, &pNode);
    }
    else
    {
        result = get_new_node(key, pvalue, &i, &pNode);
    }

    if (CACHE_RESULT_IS_OK(result))
    {
        ss_dassert(pNode);

        result = m_pStorage->put_value(key, pvalue);

        if (CACHE_RESULT_IS_OK(result))
        {
            if (existed)
            {
                ++m_stats.updates;
                ss_dassert(m_stats.size >= pNode->size());
                m_stats.size -= pNode->size();
            }
            else
            {
                ++m_stats.items;
            }

            pNode->reset(&i->first, value_size);
            m_stats.size += pNode->size();

            move_to_head(pNode);
        }
        else if (!existed)
        {
            MXS_ERROR("Could not put a value to the storage.");
            free_node(i);
        }
    }

    return result;
}

cache_result_t LRUStorage::do_del_value(const CACHE_KEY& key)
{
    cache_result_t result = CACHE_RESULT_NOT_FOUND;

    NodesByKey::iterator i = m_nodes_by_key.find(key);
    bool existed = (i != m_nodes_by_key.end());

    if (existed)
    {
        result = m_pStorage->del_value(key);

        if (CACHE_RESULT_IS_OK(result) || CACHE_RESULT_IS_NOT_FOUND(result))
        {
            // If it wasn't found, we'll assume it was because ttl has hit in.
            ++m_stats.deletes;

            ss_dassert(m_stats.size >= i->second->size());
            ss_dassert(m_stats.items > 0);

            m_stats.size -= i->second->size();
            --m_stats.items;

            free_node(i);
        }
    }

    return result;
}

cache_result_t LRUStorage::do_get_head(CACHE_KEY* pKey, GWBUF** ppValue) const
{
    cache_result_t result = CACHE_RESULT_NOT_FOUND;

    // Since it's the head it's unlikely to have happened, but we need to loop to
    // cater for the case that ttl has hit in.
    while (m_pHead && (CACHE_RESULT_IS_NOT_FOUND(result)))
    {
        ss_dassert(m_pHead->key());
        result = do_get_value(*m_pHead->key(),
                              CACHE_FLAGS_INCLUDE_STALE, CACHE_USE_CONFIG_TTL, CACHE_USE_CONFIG_TTL,
                              ppValue);
    }

    if (CACHE_RESULT_IS_OK(result))
    {
        *pKey = *m_pHead->key();
    }

    return result;
}

cache_result_t LRUStorage::do_get_tail(CACHE_KEY* pKey, GWBUF** ppValue) const
{
    cache_result_t result = CACHE_RESULT_NOT_FOUND;

    // We need to loop to cater for the case that ttl has hit in.
    while (m_pTail && CACHE_RESULT_IS_NOT_FOUND(result))
    {
        ss_dassert(m_pTail->key());
        result = peek_value(*m_pTail->key(), CACHE_FLAGS_INCLUDE_STALE, ppValue);
    }

    if (CACHE_RESULT_IS_OK(result))
    {
        *pKey = *m_pTail->key();
    }

    return result;
}

cache_result_t LRUStorage::do_get_size(uint64_t* pSize) const
{
    *pSize = m_stats.size;
    return CACHE_RESULT_OK;
}

cache_result_t LRUStorage::do_get_items(uint64_t* pItems) const
{
    *pItems = m_stats.items;
    return CACHE_RESULT_OK;
}

cache_result_t LRUStorage::access_value(access_approach_t approach,
                                        const CACHE_KEY& key,
                                        uint32_t flags,
                                        uint32_t soft_ttl,
                                        uint32_t hard_ttl,
                                        GWBUF** ppValue) const
{
    cache_result_t result = CACHE_RESULT_NOT_FOUND;

    NodesByKey::iterator i = m_nodes_by_key.find(key);
    bool existed = (i != m_nodes_by_key.end());

    if (existed)
    {
        result = m_pStorage->get_value(key, flags, soft_ttl, hard_ttl, ppValue);

        if (CACHE_RESULT_IS_OK(result))
        {
            ++m_stats.hits;

            if (approach == APPROACH_GET)
            {
                move_to_head(i->second);
            }
        }
        else if (CACHE_RESULT_IS_NOT_FOUND(result))
        {
            ++m_stats.misses;

            if (!CACHE_RESULT_IS_STALE(result))
            {
                // If it wasn't just stale we'll remove it.
                free_node(i);
            }
        }
    }
    else
    {
        ++m_stats.misses;
    }

    return result;
}

/**
 * Free the data associated with the least recently used node,
 * but not the node itself.
 *
 * @return The node itself, for reuse.
 */
LRUStorage::Node* LRUStorage::vacate_lru()
{
    ss_dassert(m_pTail);

    Node* pNode = NULL;

    if (free_node_data(m_pTail))
    {
        pNode = m_pTail;

        remove_node(pNode);
    }

    return pNode;
}

/**
 * Free the data associated with sufficient number of least recently used nodes,
 * to make the required space available. All nodes themselves, but the last node
 * are also freed.
 *
 * @return The last node whose data was freed, for reuse.
 */
LRUStorage::Node* LRUStorage::vacate_lru(size_t needed_space)
{
    Node* pNode = NULL;

    size_t freed_space = 0;
    bool error = false;

    while (!error && m_pTail && (freed_space < needed_space))
    {
        size_t size = m_pTail->size();

        if (free_node_data(m_pTail))
        {
            freed_space += size;

            pNode = m_pTail;

            remove_node(pNode);

            if (freed_space < needed_space)
            {
                delete pNode;
                pNode = NULL;
            }
        }
        else
        {
            error = true;
        }
    }

    if (pNode)
    {
        pNode->reset();
    }

    return pNode;
}

/**
 * Free the data associated with a node.
 *
 * @return True, if the data could be freed, false otherwise.
 */
bool LRUStorage::free_node_data(Node* pNode)
{
    bool success = true;

    const CACHE_KEY* pkey = pNode->key();
    ss_dassert(pkey);

    NodesByKey::iterator i = m_nodes_by_key.find(*pkey);

    if (i == m_nodes_by_key.end())
    {
        ss_dassert(!true);
        MXS_ERROR("Item in LRU list was not found in key mapping.");
    }

    cache_result_t result = m_pStorage->del_value(*pkey);

    if (CACHE_RESULT_IS_OK(result) || CACHE_RESULT_IS_NOT_FOUND(result))
    {
        if (CACHE_RESULT_IS_NOT_FOUND(result))
        {
            ss_dassert(!true);
            MXS_ERROR("Item in LRU list was not found in storage.");
        }

        if (i != m_nodes_by_key.end())
        {
            m_nodes_by_key.erase(i);
        }

        ss_dassert(m_stats.size >= pNode->size());
        ss_dassert(m_stats.items > 0);

        m_stats.size -= pNode->size();
        m_stats.items -= 1;
        m_stats.evictions += 1;
    }
    else
    {
        ss_dassert(!true);
        MXS_ERROR("Could not remove value from storage, cannot "
                  "remove from LRU list or key mapping either.");
        success = false;
    }

    return success;
}

/**
 * Free a node and update head/tail accordingly.
 *
 * @param pNode  The node to be freed.
 */
void LRUStorage::free_node(Node* pNode) const
{
    remove_node(pNode);
    delete pNode;

    ss_dassert(!m_pHead || (m_pHead->prev() == NULL));
    ss_dassert(!m_pTail || (m_pTail->next() == NULL));
}

/**
 * Free the node referred to by the iterator and update head/tail accordingly.
 *
 * @param i   The map iterator.
 */
void LRUStorage::free_node(NodesByKey::iterator& i) const
{
    free_node(i->second); // A Node
    m_nodes_by_key.erase(i);
}

/**
 * Remove a node and update head/tail accordingly.
 *
 * @param pNode  The node to be removed.
 */
void LRUStorage::remove_node(Node* pNode) const
{
    ss_dassert(m_pHead->prev() == NULL);
    ss_dassert(m_pTail->next() == NULL);

    if (m_pHead == pNode)
    {
        m_pHead = m_pHead->next();
    }

    if (m_pTail == pNode)
    {
        m_pTail = m_pTail->prev();
    }

    pNode->remove();

    ss_dassert(!m_pHead || (m_pHead->prev() == NULL));
    ss_dassert(!m_pTail || (m_pTail->next() == NULL));
}

/**
 * Move a node to head.
 *
 * @param pNode  The node to be moved to head.
 */
void LRUStorage::move_to_head(Node* pNode) const
{
    ss_dassert(!m_pHead || (m_pHead->prev() == NULL));
    ss_dassert(!m_pTail || (m_pTail->next() == NULL));

    if (m_pTail == pNode)
    {
        m_pTail = pNode->prev();
    }

    m_pHead = pNode->prepend(m_pHead);

    if (!m_pTail)
    {
        m_pTail = m_pHead;
    }

    ss_dassert(m_pHead);
    ss_dassert(m_pTail);
    ss_dassert((m_pHead != m_pTail) || (m_pHead == pNode));
    ss_dassert(m_pHead->prev() == NULL);
    ss_dassert(m_pTail->next() == NULL);
}

cache_result_t LRUStorage::get_existing_node(NodesByKey::iterator& i, const GWBUF* pValue, Node** ppNode)
{
    cache_result_t result = CACHE_RESULT_OK;

    size_t value_size = GWBUF_LENGTH(pValue);

    if (value_size > m_max_size)
    {
        // If the size of the new item is more than what is allowed in total,
        // we must remove the value.
        const CACHE_KEY* pkey = i->second->key();
        ss_dassert(pkey);

        result = do_del_value(*pkey);

        if (!CACHE_RESULT_IS_ERROR(result))
        {
            // If we failed to remove the value, we do not have enough space.
            result = CACHE_RESULT_OUT_OF_RESOURCES;
        }
    }
    else
    {
        Node* pNode = i->second;

        size_t new_size = m_stats.size - pNode->size() + value_size;

        if (new_size > m_max_size)
        {
            ss_dassert(value_size > pNode->size());

            // We move it to the front, so that we do not have to deal with the case
            // that 'pNode' is subject to removal.
            move_to_head(pNode);

            size_t extra_size = value_size - pNode->size();

            Node* pVacant_node = vacate_lru(extra_size);

            if (pVacant_node)
            {
                // We won't be using the node.
                free_node(pVacant_node);

                *ppNode = pNode;
            }
            else
            {
                ss_dassert(!true);
                // If we could not vacant nodes, we are hosed.
                result = CACHE_RESULT_ERROR;
            }
        }
        else
        {
            ss_dassert(m_stats.items <= m_max_count);
            *ppNode = pNode;
        }
    }

    return result;
}

cache_result_t LRUStorage::get_new_node(const CACHE_KEY& key,
                                        const GWBUF* pValue,
                                        NodesByKey::iterator* pI,
                                        Node** ppNode)
{
    cache_result_t result = CACHE_RESULT_OK;

    size_t value_size = GWBUF_LENGTH(pValue);
    size_t new_size = m_stats.size + value_size;

    Node* pNode = NULL;

    if ((new_size > m_max_size) || (m_stats.items == m_max_count))
    {
        if (new_size > m_max_size)
        {
            pNode = vacate_lru(value_size);
        }
        else if (m_stats.items == m_max_count)
        {
            ss_dassert(m_stats.items == m_max_count);
            pNode = vacate_lru();
        }

        if (!pNode)
        {
            result = CACHE_RESULT_ERROR;
        }
    }
    else
    {
        pNode = new (std::nothrow) Node;
    }

    if (pNode)
    {
        try
        {
            std::pair<NodesByKey::iterator, bool> rv;
            rv = m_nodes_by_key.insert(std::make_pair(key, pNode));
            ss_dassert(rv.second); // If true, the item was inserted as new (and not updated).
            *pI = rv.first;
        }
        catch (const std::exception& x)
        {
            delete pNode;
            pNode = NULL;
            result = CACHE_RESULT_OUT_OF_RESOURCES;
        }
    }

    if (CACHE_RESULT_IS_OK(result))
    {
        ss_dassert(pNode);
        *ppNode = pNode;
    }

    return result;
}

static void set_integer(json_t* pObject, const char* zName, size_t value)
{
    json_t* pValue = json_integer(value);

    if (pValue)
    {
        json_object_set(pObject, zName, pValue);
        json_decref(pValue);
    }
}

void LRUStorage::Stats::fill(json_t* pObject) const
{
    set_integer(pObject, "size", size);
    set_integer(pObject, "items", items);
    set_integer(pObject, "hits", hits);
    set_integer(pObject, "misses", misses);
    set_integer(pObject, "updates", updates);
    set_integer(pObject, "deletes", deletes);
    set_integer(pObject, "evictions", evictions);
}
