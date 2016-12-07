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
#include "lrustorage.h"

LRUStorage::LRUStorage(Storage* pstorage, size_t max_count, size_t max_size)
    : pstorage_(pstorage)
    , max_count_(max_count)
    , max_size_(max_size)
    , phead_(NULL)
    , ptail_(NULL)
{
}

LRUStorage::~LRUStorage()
{
}

cache_result_t LRUStorage::get_key(const char* zdefault_db,
                                   const GWBUF* pquery,
                                   CACHE_KEY* pkey)
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
                                        GWBUF** ppvalue)
{
    NodesPerKey::iterator i = nodes_per_key_.find(key);
    bool existed = (i != nodes_per_key_.end());

    cache_result_t result = pstorage_->get_value(key, flags, ppvalue);

    if (result == CACHE_RESULT_OK)
    {
        ++stats_.hits;

        if (existed)
        {
            if (ptail_ == i->second)
            {
                ptail_ = i->second->prev();
            }

            phead_ = i->second->prepend(phead_);
        }
        else
        {
            MXS_ERROR("Item found in storage, but not in key mapping.");
        }
    }
    else
    {
        ++stats_.misses;

        if (existed && (result == CACHE_RESULT_NOT_FOUND))
        {
            MXS_ERROR("Item was not found in storage, but was found in key mapping.");
        }
    }

    return result;
}

cache_result_t LRUStorage::do_put_value(const CACHE_KEY& key,
                                        const GWBUF* pvalue)
{
    cache_result_t result = CACHE_RESULT_ERROR;

    size_t value_size = GWBUF_LENGTH(pvalue);
    size_t new_size = stats_.size + value_size;

    Node* pnode = NULL;

    NodesPerKey::iterator i = nodes_per_key_.find(key);
    bool existed = (i != nodes_per_key_.end());

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

                pnode = free_lru(value_size);
            }
            else
            {
                ss_dassert(stats_.items == max_count_);
                MXS_NOTICE("Max count %lu reached, removing least recently used.", max_count_);
                pnode = free_lru();
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
                std::pair<NodesPerKey::iterator, bool>
                    rv = nodes_per_key_.insert(std::make_pair(key, pnode));
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

            if (ptail_ == pnode)
            {
                ptail_ = pnode->prev();
            }

            phead_ = pnode->prepend(phead_);

            if (!ptail_)
            {
                ptail_ = phead_;
            }
        }
        else if (!existed)
        {
            MXS_ERROR("Could not put a value to the storage.");
            nodes_per_key_.erase(i);
            delete pnode;
        }
    }

    return result;
}

cache_result_t LRUStorage::do_del_value(const CACHE_KEY& key)
{
    NodesPerKey::iterator i = nodes_per_key_.find(key);

    cache_result_t result = pstorage_->del_value(key);

    if (result == CACHE_RESULT_OK)
    {
        ++stats_.deletes;

        if (i == nodes_per_key_.end())
        {
            Node* pnode = i->second;

            ss_dassert(stats_.size > pnode->size());
            ss_dassert(stats_.items > 0);

            stats_.size -= pnode->size();
            --stats_.items;

            phead_ = pnode->remove();
            delete pnode;

            if (!phead_)
            {
                ptail_ = NULL;
            }

            nodes_per_key_.erase(i);
        }
        else
        {
            MXS_ERROR("Key was found from storage, but not from LRU register.");
        }
    }

    return result;
}

/**
 * Free the data associated with the least recently used node.
 *
 * @return The node itself, for reuse.
 */
LRUStorage::Node* LRUStorage::free_lru()
{
    ss_dassert(ptail_);

    Node* pnode = NULL;

    if (free_node_data(ptail_))
    {
        pnode = ptail_;
        ptail_ = ptail_->remove();
    }

    return pnode;
}

/**
 * Free the data associated with sufficient number of least recently used nodes,
 * to make the required space available.
 *
 * @return The last node whose data was freed, for reuse.
 */
LRUStorage::Node* LRUStorage::free_lru(size_t needed_space)
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
            ptail_ = ptail_->remove();

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

    NodesPerKey::iterator i = nodes_per_key_.find(*pkey);

    if (i == nodes_per_key_.end())
    {
        MXS_ERROR("Item in LRU list was not found in key mapping.");
    }

    cache_result_t result = pstorage_->del_value(*pkey);

    switch (result)
    {
    case CACHE_RESULT_NOT_FOUND:
        MXS_ERROR("Item in LRU list was not found in storage.");
    case CACHE_RESULT_OK:
        if (i != nodes_per_key_.end())
        {
            nodes_per_key_.erase(i);
        }

        ss_dassert(stats_.size >= pnode->size());
        ss_dassert(stats_.items > 0);

        stats_.size -= pnode->size();
        stats_.items -= 1;
        stats_.evictions += 1;
        break;

    default:
        MXS_ERROR("Could not remove value from storage, cannot "
                  "remove from LRU list or key mapping either.");
        success = false;
    }

    return success;
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
