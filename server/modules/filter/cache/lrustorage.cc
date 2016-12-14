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

LRUStorage::LRUStorage(Storage* pstorage, size_t max_count, size_t max_size)
    : pstorage_(pstorage)
    , max_count_(max_count != 0 ? max_count : UINT64_MAX)
    , max_size_(max_size != 0 ? max_size : UINT64_MAX)
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

        if (pstorage_->get_info(what, &pstorage_info) == CACHE_RESULT_OK)
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
    NodesByKey::iterator i = nodes_by_key_.find(key);
    bool existed = (i != nodes_by_key_.end());

    cache_result_t result = pstorage_->get_value(key, flags, ppvalue);

    if ((result == CACHE_RESULT_OK) || (result == CACHE_RESULT_STALE))
    {
        ++stats_.hits;

        if (existed)
        {
            move_to_head(i->second);
        }
        else
        {
            ss_dassert(!true);
            MXS_ERROR("Item found in storage, but not in key mapping.");
        }
    }
    else
    {
        ++stats_.misses;

        if (existed && (result == CACHE_RESULT_NOT_FOUND))
        {
            // We'll assume this is because ttl has hit in. We need to remove
            // the node and the mapping.
            free_node(i);
        }
    }

    return result;
}

cache_result_t LRUStorage::do_put_value(const CACHE_KEY& key, const GWBUF* pvalue)
{
    cache_result_t result = CACHE_RESULT_ERROR;

    size_t value_size = GWBUF_LENGTH(pvalue);
    size_t new_size = stats_.size + value_size;

    Node* pnode = NULL;

    NodesByKey::iterator i = nodes_by_key_.find(key);
    bool existed = (i != nodes_by_key_.end());

    if (existed)
    {
        // TODO: Also in this case max_size_ needs to be honoured.
        pnode = i->second;
    }
    else
    {
        if ((new_size > max_size_) || (stats_.items == max_count_))
        {
            if (new_size > max_size_)
            {
                MXS_NOTICE("New size %lu > max size %lu. Removing least recently used.",
                           new_size, max_size_);

                pnode = vacate_lru(value_size);
            }
            else
            {
                ss_dassert(stats_.items == max_count_);
                pnode = vacate_lru();
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
                std::pair<NodesByKey::iterator, bool>
                    rv = nodes_by_key_.insert(std::make_pair(key, pnode));
                ss_dassert(rv.second);

                i = rv.first;
            }
            catch (const std::exception& x)
            {
                delete pnode;
                pnode = NULL;
                result = CACHE_RESULT_OUT_OF_RESOURCES;
            }
        }
    }

    if (pnode)
    {
        result = pstorage_->put_value(key, pvalue);

        if (result == CACHE_RESULT_OK)
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
    NodesByKey::iterator i = nodes_by_key_.find(key);
    bool existed = (i != nodes_by_key_.end());

    cache_result_t result = pstorage_->del_value(key);

    if (result == CACHE_RESULT_OK)
    {
        if (!existed)
        {
            ss_dassert(!true);
            MXS_ERROR("Key was found from storage, but not from LRU register.");
        }
    }

    if (existed && ((result == CACHE_RESULT_OK) || (result == CACHE_RESULT_NOT_FOUND)))
    {
        // If it wasn't found, we'll assume it was because ttl has hit in.
        ++stats_.deletes;

        ss_dassert(stats_.size >= i->second->size());
        ss_dassert(stats_.items > 0);

        stats_.size -= i->second->size();
        --stats_.items;

        free_node(i);
    }

    return result;
}

cache_result_t LRUStorage::do_get_head(CACHE_KEY* pKey, GWBUF** ppValue) const
{
    cache_result_t result = CACHE_RESULT_NOT_FOUND;

    // Since it's the head it's unlikely to have happened, but we need to loop to
    // cater for the case that ttl has hit in.
    while (phead_ && (result == CACHE_RESULT_NOT_FOUND))
    {
        ss_dassert(phead_->key());
        result = do_get_value(*phead_->key(), CACHE_FLAGS_INCLUDE_STALE, ppValue);
    }

    if (result == CACHE_RESULT_OK)
    {
        *pKey = *phead_->key();
    }

    return result;
}

cache_result_t LRUStorage::do_get_tail(CACHE_KEY* pKey, GWBUF** ppValue) const
{
    cache_result_t result = CACHE_RESULT_NOT_FOUND;

    // We need to loop to cater for the case that ttl has hit in.
    while (ptail_ && (result == CACHE_RESULT_NOT_FOUND))
    {
        ss_dassert(ptail_->key());
        result = do_get_value(*ptail_->key(), CACHE_FLAGS_INCLUDE_STALE, ppValue);
    }

    if (result == CACHE_RESULT_OK)
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

    switch (result)
    {
    case CACHE_RESULT_NOT_FOUND:
        ss_dassert(!true);
        MXS_ERROR("Item in LRU list was not found in storage.");
    case CACHE_RESULT_OK:
        if (i != nodes_by_key_.end())
        {
            nodes_by_key_.erase(i);
        }

        ss_dassert(stats_.size >= pnode->size());
        ss_dassert(stats_.items > 0);

        stats_.size -= pnode->size();
        stats_.items -= 1;
        stats_.evictions += 1;
        break;

    default:
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
