#pragma once
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

    /**
     * @see Storage::get_key
     */
    cache_result_t get_key(const char* zDefaultDb,
                           const GWBUF* pQuery,
                           CACHE_KEY* pKey) const;

protected:
    LRUStorage(const CACHE_STORAGE_CONFIG& config, Storage* pstorage);

    /**
     * @see Storage::get_info
     */
    cache_result_t do_get_info(uint32_t what, json_t** ppInfo) const;

    /**
     * @see Storage::get_value
     */
    cache_result_t do_get_value(const CACHE_KEY& key,
                                uint32_t flags,
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
                                GWBUF** ppValue) const;

    cache_result_t peek_value(const CACHE_KEY& key,
                              uint32_t flags,
                              GWBUF** ppValue) const
    {
        return access_value(APPROACH_PEEK, key, flags, ppValue);
    }

    /**
     * The Node class is used for maintaining LRU information.
     */
    class Node
    {
    public:
        Node()
            : pkey_(NULL)
            , size_(0)
            , pnext_(NULL)
            , pprev_(NULL)
        {}
        ~Node()
        {
            remove();
        }

        const CACHE_KEY* key() const
        {
            return pkey_;
        }
        size_t size() const
        {
            return size_;
        }
        Node* next() const
        {
            return pnext_;
        }
        Node* prev() const
        {
            return pprev_;
        }

        /**
         * Move the node before the node provided as argument.
         *
         * @param  pnode  The node in front of which this should be moved.
         * @return This node.
         */
        Node* prepend(Node* pnode)
        {
            if (pnode && (pnode != this))
            {
                if (pprev_)
                {
                    pprev_->pnext_ = pnext_;
                }

                if (pnext_)
                {
                    pnext_->pprev_ = pprev_;
                }

                if (pnode->pprev_)
                {
                    pnode->pprev_->pnext_ = this;
                }

                pprev_ = pnode->pprev_;
                pnext_ = pnode;

                pnode->pprev_ = this;
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
            if (pprev_)
            {
                pprev_->pnext_ = pnext_;
            }

            if (pnext_)
            {
                pnext_->pprev_ = pprev_;
            }

            Node* pnode = (pprev_ ? pprev_ : pnext_);

            pprev_ = NULL;
            pnext_ = NULL;

            return pnode;
        }

        void reset(const CACHE_KEY* pkey = NULL, size_t size = 0)
        {
            pkey_ = pkey;
            size_ = size;
        }

    private:
        const CACHE_KEY* pkey_;  /*< Points at the key stored in nodes_by_key_ below. */
        size_t           size_;  /*< The size of the data referred to by pkey_. */
        Node*            pnext_; /*< The next node in the LRU list. */
        Node*            pprev_; /*< The previous node in the LRU list. */
    };

    typedef std::tr1::unordered_map<CACHE_KEY, Node*> NodesByKey;

    Node* vacate_lru();
    Node* vacate_lru(size_t space);
    bool free_node_data(Node* pnode);
    void free_node(Node* pnode) const;
    void free_node(NodesByKey::iterator& i) const;
    void remove_node(Node* pnode) const;
    void move_to_head(Node* pnode) const;

    cache_result_t get_existing_node(NodesByKey::iterator& i, const GWBUF* pvalue, Node** ppnode);
    cache_result_t get_new_node(const CACHE_KEY& key,
                                const GWBUF* pvalue,
                                NodesByKey::iterator* pI,
                                Node** ppnode);

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

        void fill(json_t* pbject) const;

        uint64_t size;       /*< The total size of the stored values. */
        uint64_t items;      /*< The number of stored items. */
        uint64_t hits;       /*< How many times a key was found in the cache. */
        uint64_t misses;     /*< How many times a key was not found in the cache. */
        uint64_t updates;    /*< How many times an existing key in the cache was updated. */
        uint64_t deletes;    /*< How many times an existing key in the cache was deleted. */
        uint64_t evictions;  /*< How many times an item has been evicted from the cache. */
    };

    const CACHE_STORAGE_CONFIG config_;       /*< The configuration. */
    Storage*                   pstorage_;     /*< The actual storage. */
    const uint64_t             max_count_;    /*< The maximum number of items in the LRU list, */
    const uint64_t             max_size_;     /*< The maximum size of all cached items. */
    mutable Stats              stats_;        /*< Cache statistics. */
    mutable NodesByKey         nodes_by_key_; /*< Mapping from cache keys to corresponding Node. */
    mutable Node*              phead_;        /*< The node at the LRU list. */
    mutable Node*              ptail_;        /*< The node at bottom of the LRU list.*/
};
