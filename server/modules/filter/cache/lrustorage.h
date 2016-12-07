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

#include <maxscale/cdefs.h>
#include <tr1/unordered_map>
#include "storage.h"
#include "cachefilter.h"

class LRUStorage : public Storage
{
public:
    ~LRUStorage();

    /**
     * @see Storage::get_key
     */
    cache_result_t get_key(const char* zDefaultDb,
                           const GWBUF* pQuery,
                           CACHE_KEY* pKey);

protected:
    LRUStorage(Storage* pstorage, size_t max_count, size_t max_size);

    /**
     * Returns information about the LRU storage and the underlying real
     * storage.
     *
     * @see Storage::get_info
     */
    cache_result_t do_get_info(uint32_t what, json_t** ppInfo) const;

    /**
     * Fetches the value from the underlying storage and, if found, moves the
     * entry to the top of the LRU list.
     *
     * @see Storage::get_value
     */
    cache_result_t do_get_value(const CACHE_KEY& key,
                                uint32_t flags,
                                GWBUF** ppValue);

    /**
     * Stores the value to the underlying storage and, if successful, either
     * places the entry at or moves the existing entry to the top of the LRU
     * list.
     *
     * @see Storage::put_value
     */
    cache_result_t do_put_value(const CACHE_KEY& key,
                                const GWBUF* pValue);

    /**
     * Deletes the value from the underlying storage and, if successful, removes
     * the entry from the LRU list.
     *
     * @see Storage::del_value
     */
    cache_result_t do_del_value(const CACHE_KEY& key);

private:
    LRUStorage(const LRUStorage&);
    LRUStorage& operator = (const LRUStorage&);

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
            if (pnext_)
            {
                pnext_->pprev_ = pprev_;
            }

            if (pprev_)
            {
                pprev_->pnext_ = pnext_;
            }
        }

        const CACHE_KEY* key() const { return pkey_; }
        size_t size() const { return size_; }
        Node* next() const { return pnext_; }
        Node* prev() const { return pprev_; }

        /**
         * Move the node before the node provided as argument.
         *
         * @param  pnode  The node in front of which this should be moved.
         * @return This node.
         */
        Node* prepend(Node* pnode)
        {
            if (pnode)
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

            return pprev_ ? pprev_ : pnext_;
        }

        void reset(const CACHE_KEY* pkey = NULL, size_t size = 0)
        {
            pkey_ = pkey;
            size_ = size;
        }

    private:
        const CACHE_KEY* pkey_;  /*< Points at the key stored in nodes_per_key_ below. */
        size_t           size_;  /*< The size of the data referred to by pkey_. */
        Node*            pnext_; /*< The next node in the LRU list. */
        Node*            pprev_; /*< The previous node in the LRU list. */
    };

    Node* free_lru();
    Node* free_lru(size_t space);
    bool free_node_data(Node* pnode);

private:
    typedef std::tr1::unordered_map<CACHE_KEY, Node*> NodesPerKey;

    Storage*    pstorage_;      /*< The actual storage. */
    size_t      max_count_;     /*< The maximum number of items in the LRU list, */
    size_t      max_size_;      /*< The maximum size of all cached items. */
    size_t      count_;         /*< The current count of cached items. */
    size_t      size_;          /*< The current size of all cached items. */
    NodesPerKey nodes_per_key_; /*< Mapping from cache keys to corresponding Node. */
    Node*       phead_;         /*< The node at the LRU list. */
    Node*       ptail_;         /*< The node at bottom of the LRU list.*/
};
