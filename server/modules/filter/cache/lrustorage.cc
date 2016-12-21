/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl.
 *
 * Change Date: 2019-07-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#define MXS_MODULE_NAME "cache"
#include "lrustorage.hh"

LRUStorage::LRUStorage(const CACHE_STORAGE_CONFIG& config, Storage* pstorage)
    : config_(config)
    , pstorage_(pstorage)
    , max_count_(config.max_count != 0 ? config.max_count : UINT64_MAX)
    , max_size_(config.max_size != 0 ? config.max_size : UINT64_MAX)
    , phead_(NULL)
    , ptail_(NULL)
{
}

LRUStorage::~LRUStorage()
{
    Node* pnode = phead_;

    while (phead_)
    {
        free_node(phead_); // Adjusts phead_
    }

    delete pstorage_;
}

void LRUStorage::get_config(CACHE_STORAGE_CONFIG* pConfig)
{
    *pConfig = config_;
}

cache_result_t LRUStorage::get_key(const char* zdefault_db,
                                   const GWBUF* pquery,
                                   CACHE_KEY* pkey) const
{
    return pstorage_->get_key(zdefault_db, pquery, pkey);
}

cache_result_t LRUStorage::do_get_info(uint32_t what,
                                       json_t** ppinfo) const
{
    *ppinfo = json_object();

    if (*ppinfo)
    {
        json_t* plru = json_object();

        if (plru)
        {
            stats_.fill(plru);

            json_object_set(*ppinfo, "lru", plru);
            json_decref(plru);
        }

        json_t* pstorage_info;

        cache_result_t result = pstorage_->get_info(what, &pstorage_info);

        if (CACHE_RESULT_IS_OK(result))
        {
            json_object_set(*ppinfo, "real_storage", pstorage_info);
            json_decref(pstorage_info);
        }
    }

    return *ppinfo ? CACHE_RESULT_OK : CACHE_RESULT_OUT_OF_RESOURCES;
}

cache_result_t LRUStorage::do_get_value(const CACHE_KEY& key,
                                        uint32_t flags,
                                        GWBUF** ppvalue) const
{
    return access_value(APPROACH_GET, key, flags, ppvalue);
}

cache_result_t LRUStorage::do_put_value(const CACHE_KEY& key, const GWBUF* pvalue)
{
    cache_result_t result = CACHE_RESULT_ERROR;

    size_t value_size = GWBUF_LENGTH(pvalue);

    Node* pnode = NULL;

    NodesByKey::iterator i = nodes_by_key_.find(key);
    bool existed = (i != nodes_by_key_.end());

    if (existed)
    {
        result = get_existing_node(i, pvalue, &pnode);
    }
    else
    {
        result = get_new_node(key, pvalue, &i, &pnode);
    }

    if (CACHE_RESULT_IS_OK(result))
    {
        ss_dassert(pnode);

        result = pstorage_->put_value(key, pvalue);

        if (CACHE_RESULT_IS_OK(result))
        {
            if (existed)
            {
                ++stats_.updates;
                ss_dassert(stats_.size >= pnode->size());
                stats_.size -= pnode->size();
            }
            else
            {
                ++stats_.items;
            }

            pnode->reset(&i->first, value_size);
            stats_.size += pnode->size();

            move_to_head(pnode);
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

    NodesByKey::iterator i = nodes_by_key_.find(key);
    bool existed = (i != nodes_by_key_.end());

    if (existed)
    {
        result = pstorage_->del_value(key);

        if (CACHE_RESULT_IS_OK(result) || CACHE_RESULT_IS_NOT_FOUND(result))
        {
            // If it wasn't found, we'll assume it was because ttl has hit in.
            ++stats_.deletes;

            ss_dassert(stats_.size >= i->second->size());
            ss_dassert(stats_.items > 0);

            stats_.size -= i->second->size();
            --stats_.items;

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
    while (phead_ && (CACHE_RESULT_IS_NOT_FOUND(result)))
    {
        ss_dassert(phead_->key());
        result = do_get_value(*phead_->key(), CACHE_FLAGS_INCLUDE_STALE, ppValue);
    }

    if (CACHE_RESULT_IS_OK(result))
    {
        *pKey = *phead_->key();
    }

    return result;
}

cache_result_t LRUStorage::do_get_tail(CACHE_KEY* pKey, GWBUF** ppValue) const
{
    cache_result_t result = CACHE_RESULT_NOT_FOUND;

    // We need to loop to cater for the case that ttl has hit in.
    while (ptail_ && CACHE_RESULT_IS_NOT_FOUND(result))
    {
        ss_dassert(ptail_->key());
        result = peek_value(*ptail_->key(), CACHE_FLAGS_INCLUDE_STALE, ppValue);
    }

    if (CACHE_RESULT_IS_OK(result))
    {
        *pKey = *ptail_->key();
    }

    return result;
}

cache_result_t LRUStorage::do_get_size(uint64_t* pSize) const
{
    *pSize = stats_.size;
    return CACHE_RESULT_OK;
}

cache_result_t LRUStorage::do_get_items(uint64_t* pItems) const
{
    *pItems = stats_.items;
    return CACHE_RESULT_OK;
}

cache_result_t LRUStorage::access_value(access_approach_t approach,
                                        const CACHE_KEY& key,
                                        uint32_t flags,
                                        GWBUF** ppvalue) const
{
    cache_result_t result = CACHE_RESULT_NOT_FOUND;

    NodesByKey::iterator i = nodes_by_key_.find(key);
    bool existed = (i != nodes_by_key_.end());

    if (existed)
    {
        result = pstorage_->get_value(key, flags, ppvalue);

        if (CACHE_RESULT_IS_OK(result))
        {
            ++stats_.hits;

            if (approach == APPROACH_GET)
            {
                move_to_head(i->second);
            }
        }
        else if (CACHE_RESULT_IS_NOT_FOUND(result))
        {
            ++stats_.misses;

            if (!CACHE_RESULT_IS_STALE(result))
            {
                // If it wasn't just stale we'll remove it.
                free_node(i);
            }
        }
    }
    else
    {
        ++stats_.misses;
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
    ss_dassert(ptail_);

    Node* pnode = NULL;

    if (free_node_data(ptail_))
    {
        pnode = ptail_;

        remove_node(pnode);
    }

    return pnode;
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
    Node* pnode = NULL;

    size_t freed_space = 0;
    bool error = false;

    while (!error && ptail_ && (freed_space < needed_space))
    {
        size_t size = ptail_->size();

        if (free_node_data(ptail_))
        {
            freed_space += size;

            pnode = ptail_;

            remove_node(pnode);

            if (freed_space < needed_space)
            {
                delete pnode;
                pnode = NULL;
            }
        }
        else
        {
            error = true;
        }
    }

    if (pnode)
    {
        pnode->reset();
    }

    return pnode;
}

/**
 * Free the data associated with a node.
 *
 * @return True, if the data could be freed, false otherwise.
 */
bool LRUStorage::free_node_data(Node* pnode)
{
    bool success = true;

    const CACHE_KEY* pkey = pnode->key();
    ss_dassert(pkey);

    NodesByKey::iterator i = nodes_by_key_.find(*pkey);

    if (i == nodes_by_key_.end())
    {
        ss_dassert(!true);
        MXS_ERROR("Item in LRU list was not found in key mapping.");
    }

    cache_result_t result = pstorage_->del_value(*pkey);

    if (CACHE_RESULT_IS_OK(result) || CACHE_RESULT_IS_NOT_FOUND(result))
    {
        if (CACHE_RESULT_IS_NOT_FOUND(result))
        {
            ss_dassert(!true);
            MXS_ERROR("Item in LRU list was not found in storage.");
        }

        if (i != nodes_by_key_.end())
        {
            nodes_by_key_.erase(i);
        }

        ss_dassert(stats_.size >= pnode->size());
        ss_dassert(stats_.items > 0);

        stats_.size -= pnode->size();
        stats_.items -= 1;
        stats_.evictions += 1;
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
 * @param pnode  The node to be freed.
 */
void LRUStorage::free_node(Node* pnode) const
{
    remove_node(pnode);
    delete pnode;

    ss_dassert(!phead_ || (phead_->prev() == NULL));
    ss_dassert(!ptail_ || (ptail_->next() == NULL));
}

/**
 * Free the node referred to by the iterator and update head/tail accordingly.
 *
 * @param i   The map iterator.
 */
void LRUStorage::free_node(NodesByKey::iterator& i) const
{
    free_node(i->second); // A Node
    nodes_by_key_.erase(i);
}

/**
 * Remove a node and update head/tail accordingly.
 *
 * @param pnode  The node to be removed.
 */
void LRUStorage::remove_node(Node* pnode) const
{
    ss_dassert(phead_->prev() == NULL);
    ss_dassert(ptail_->next() == NULL);

    if (phead_ == pnode)
    {
        phead_ = phead_->next();
    }

    if (ptail_ == pnode)
    {
        ptail_ = ptail_->prev();
    }

    pnode->remove();

    ss_dassert(!phead_ || (phead_->prev() == NULL));
    ss_dassert(!ptail_ || (ptail_->next() == NULL));
}

/**
 * Move a node to head.
 *
 * @param pnode  The node to be moved to head.
 */
void LRUStorage::move_to_head(Node* pnode) const
{
    ss_dassert(!phead_ || (phead_->prev() == NULL));
    ss_dassert(!ptail_ || (ptail_->next() == NULL));

    if (ptail_ == pnode)
    {
        ptail_ = pnode->prev();
    }

    phead_ = pnode->prepend(phead_);

    if (!ptail_)
    {
        ptail_ = phead_;
    }

    ss_dassert(phead_);
    ss_dassert(ptail_);
    ss_dassert((phead_ != ptail_) || (phead_ == pnode));
    ss_dassert(phead_->prev() == NULL);
    ss_dassert(ptail_->next() == NULL);
}

cache_result_t LRUStorage::get_existing_node(NodesByKey::iterator& i, const GWBUF* pvalue, Node** ppnode)
{
    cache_result_t result = CACHE_RESULT_OK;

    size_t value_size = GWBUF_LENGTH(pvalue);

    if (value_size > max_size_)
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
        Node* pnode = i->second;

        size_t new_size = stats_.size - pnode->size() + value_size;

        if (new_size > max_size_)
        {
            ss_dassert(value_size > pnode->size());

            // We move it to the front, so that we do not have to deal with the case
            // that 'pnode' is subject to removal.
            move_to_head(pnode);

            size_t extra_size = value_size - pnode->size();

            Node* pvacant_node = vacate_lru(extra_size);

            if (pvacant_node)
            {
                // We won't be using the node.
                free_node(pvacant_node);

                *ppnode = pnode;
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
            ss_dassert(stats_.items <= max_count_);
            *ppnode = pnode;
        }
    }

    return result;
}

cache_result_t LRUStorage::get_new_node(const CACHE_KEY& key,
                                        const GWBUF* pvalue,
                                        NodesByKey::iterator* pI,
                                        Node** ppnode)
{
    cache_result_t result = CACHE_RESULT_OK;

    size_t value_size = GWBUF_LENGTH(pvalue);
    size_t new_size = stats_.size + value_size;

    Node* pnode = NULL;

    if ((new_size > max_size_) || (stats_.items == max_count_))
    {
        if (new_size > max_size_)
        {
            MXS_NOTICE("New size %lu > max size %lu. Removing least recently used.",
                       new_size, max_size_);

            pnode = vacate_lru(value_size);
        }
        else if (stats_.items == max_count_)
        {
            ss_dassert(stats_.items == max_count_);
            pnode = vacate_lru();
        }

        if (!pnode)
        {
            result = CACHE_RESULT_ERROR;
        }
    }
    else
    {
        pnode = new (std::nothrow) Node;
    }

    if (pnode)
    {
        try
        {
            std::pair<NodesByKey::iterator, bool> rv;
            rv = nodes_by_key_.insert(std::make_pair(key, pnode));
            ss_dassert(rv.second); // If true, the item was inserted as new (and not updated).
            *pI = rv.first;
        }
        catch (const std::exception& x)
        {
            delete pnode;
            pnode = NULL;
            result = CACHE_RESULT_OUT_OF_RESOURCES;
        }
    }

    if (CACHE_RESULT_IS_OK(result))
    {
        ss_dassert(pnode);
        *ppnode = pnode;
    }

    return result;
}

static void set_integer(json_t* pobject, const char* zname, size_t value)
{
    json_t* pvalue = json_integer(value);

    if (pvalue)
    {
        json_object_set(pobject, zname, pvalue);
        json_decref(pvalue);
    }
}

void LRUStorage::Stats::fill(json_t* pbject) const
{
    set_integer(pbject, "size", size);
    set_integer(pbject, "items", items);
    set_integer(pbject, "hits", hits);
    set_integer(pbject, "misses", misses);
    set_integer(pbject, "updates", updates);
    set_integer(pbject, "deletes", deletes);
    set_integer(pbject, "evictions", evictions);
}
