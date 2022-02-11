/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-02-11
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#define MXS_MODULE_NAME "cache"
#include "lrustorage.hh"
#include <unordered_map>
#include <unordered_set>

using namespace std;

/**
 * @class LRUStorage::Invalidator
 *
 * Base of all invalidators.
 */
class LRUStorage::Invalidator
{
public:
    virtual ~Invalidator()
    {
    }

    /**
     * Return words to be passed on to the storage.
     *
     * @param words  Invalidation words provided by the user of the storage.
     *
     * @return The words to provide to the raw storage.
     */
    virtual const vector<string>& storage_words(const vector<string>& words) const = 0;

    /**
     * Return words to be bookkept in the node.
     *
     * @param words  Invalidation words provided by the user of the storage.
     *
     * @return The words to store in the node.
     */
    virtual const vector<string>& node_words(const vector<string>& words) const = 0;

    /**
     * Add node to bookkeeping.
     *
     * @param pNode  The node.
     */
    virtual void make_note(LRUStorage::Node* pNode) = 0;

    /**
     * Remove node from bookkeeping.
     *
     * @param pNode  The node.
     */
    virtual void remove_note(LRUStorage::Node* pNode) = 0;

    /**
     * Perform invalidation
     *
     * @param words  The invalidation words.
     *
     * @return True, if invalidation could be performed, false otherwise.
     */
    virtual bool invalidate(const vector<string>& words) = 0;

protected:
    Invalidator(LRUStorage* pOwner)
        : m_owner(*pOwner)
    {
    }

    LRUStorage& m_owner;
};


/**
 * @class LRUStorage::NullInvalidator
 *
 * An invalidator used when no invalidation need to be performed.
 */
class LRUStorage::NullInvalidator : public LRUStorage::Invalidator
{
public:
    NullInvalidator(LRUStorage* pOwner)
        : Invalidator(pOwner)
    {
    }

    ~NullInvalidator() = default;

    const vector<string>& storage_words(const vector<string>&) const override final
    {
        return s_no_words;
    }

    const vector<string>& node_words(const vector<string>&) const override final
    {
        return s_no_words;
    }

    void make_note(LRUStorage::Node* pNode) override final
    {
        mxb_assert(pNode->invalidation_words().empty());
    }

    void remove_note(LRUStorage::Node* pNode) override final
    {
        mxb_assert(pNode->invalidation_words().empty());
    }

    bool invalidate(const vector<string>& words) override final
    {
        mxb_assert(!true);
        return true;
    }

private:
    static const vector<string> s_no_words;
};

//static
const vector<string> LRUStorage::NullInvalidator::s_no_words;


/**
 * @class LRUStorage::LRUInvalidator
 *
 * Base class for FullInvalidator and StorageInvalidator.
 */
class LRUStorage::LRUInvalidator : public LRUStorage::Invalidator
{
public:
    ~LRUInvalidator() = default;

    void make_note(LRUStorage::Node* pNode) override final
    {
        const vector<string>& words = pNode->invalidation_words();

        for (auto& word : words)
        {
            mxb_assert(!word.empty());

            Nodes& nodes = m_nodes_by_word[word];

            // If a value is put multiple times in a row, then we will
            // have made a note for it already, meaning that it already
            // will be in 'nodes'. This *will* happen when a value whose
            // soft (but not hard) TTL has passed is returned as in that
            // case the caller will fetch and store the value a new.
            // However 'nodes' being a set the following "duplicate" insert
            // will be harmless.
            nodes.insert(pNode);
        }
    }

    void remove_note(LRUStorage::Node* pNode) override final
    {
        remove_note(pNode, pNode->invalidation_words());
    }

    bool invalidate(const vector<string>& words) override
    {
        bool rv = true;

        // A particular node may be invalidated by multiple words.
        // Hence we need to ensure that we don't attempt to do that
        // multiple times.
        unordered_set<Node*> invalidated;

        for (auto& word : words)
        {
            auto it = m_nodes_by_word.find(word);

            if (it != m_nodes_by_word.end())
            {
                Nodes& nodes = it->second;

                auto it = nodes.begin();

                for (; it != nodes.end(); ++it)
                {
                    Node* pNode = *it;

                    if (invalidated.count(pNode) == 0)
                    {
                        auto node_words = pNode->invalidation_words();

                        if (invalidate_node(pNode))
                        {
                            if (node_words.size() > 1)
                            {
                                // If there are multiple invalidation words associated with
                                // the node, then the node must be removed from the bookeeping
                                // of those words as well. Otherwise we have the following:
                                // SELECT * FROM t1 UNION SELECT * FROM t2 => Node stored to t1 and t2.
                                // DELETE * FROM t1                        => Node removed from t1 and deleted
                                // DELETE * FROM t2                        => Crash as node still in t2.
                                auto jt = std::find(node_words.begin(), node_words.end(), word);
                                mxb_assert(jt != node_words.end());

                                node_words.erase(jt);

                                remove_note(pNode, node_words);
                            }

                            invalidated.insert(pNode);
                            mxb_assert(nodes.count(pNode) == 1);
                        }
                        else
                        {
                            rv = false;
                            break;
                        }
                    }
                }

                nodes.erase(nodes.begin(), it);
            }

            if (!rv)
            {
                break;
            }
        }

        return rv;
    }

protected:
    LRUInvalidator(LRUStorage* pOwner)
        : Invalidator(pOwner)
    {
    }

    virtual bool invalidate_node(Node* pNode) = 0;

    void remove_note(Node* pNode, const vector<string>& words)
    {
        for (auto& word : words)
        {
            mxb_assert(!word.empty());

            Nodes& nodes = m_nodes_by_word[word];
            auto it = nodes.find(pNode);
            mxb_assert(it != nodes.end());

            nodes.erase(it);
        }
    }

private:
    using Nodes       = unordered_set<Node*>;
    using NodesByWord = unordered_map<string, Nodes>;

    NodesByWord m_nodes_by_word; // Nodes to be invalidated due to a word.
};


/**
 * @class LRUStorage::FullInvalidator
 *
 * An invalidator used when invalidation must be performed and the
 * storage provides no support for invalidation.
 */
class LRUStorage::FullInvalidator : public LRUStorage::LRUInvalidator
{
public:
    FullInvalidator(LRUStorage* pOwner)
        : LRUInvalidator(pOwner)
    {
    }

    ~FullInvalidator() = default;

    const vector<string>& storage_words(const vector<string>&) const override final
    {
        return s_no_words;
    }

    const vector<string>& node_words(const vector<string>& invalidation_words) const override final
    {
        return invalidation_words;
    }

    bool invalidate_node(Node* pNode) override final
    {
        return m_owner.invalidate(pNode, LRUStorage::Context::INVALIDATION);
    }

private:
    static const vector<string> s_no_words;
};

//static
const vector<string> LRUStorage::FullInvalidator::s_no_words;


/**
 * @class LRUStorage::StorageInvalidator
 *
 * An invalidator used when invalidation must be performed and the
 * storage provides support for invalidation.
 */
class LRUStorage::StorageInvalidator : public LRUStorage::LRUInvalidator
{
public:
    StorageInvalidator(LRUStorage* pOwner)
        : LRUInvalidator(pOwner)
    {
    }

    ~StorageInvalidator() = default;

    const vector<string>& storage_words(const vector<string>& invalidation_words) const override final
    {
        return invalidation_words;
    }

    const vector<string>& node_words(const vector<string>& invalidation_words) const override final
    {
        return invalidation_words;
    }

    bool invalidate(const vector<string>& words) override
    {
        bool rv = LRUInvalidator::invalidate(words);

        if (rv)
        {
            if (m_owner.storage()->invalidate(nullptr, words, nullptr) != CACHE_RESULT_OK)
            {
                rv = false;
            }
        }

        return rv;
    }

    bool invalidate_node(Node* pNode) override final
    {
        return m_owner.invalidate(pNode, LRUStorage::Context::LRU_INVALIDATION);
    }
};


/**
 * @class LRUStorage
 */
LRUStorage::LRUStorage(const Config& config, Storage* pStorage)
    : m_config(config)
    , m_pStorage(pStorage)
    , m_max_count(config.max_count != 0 ? config.max_count : UINT64_MAX)
    , m_max_size(config.max_size != 0 ? config.max_size : UINT64_MAX)
    , m_pHead(NULL)
    , m_pTail(NULL)
{
    if (m_config.invalidate == CACHE_INVALIDATE_NEVER)
    {
        m_sInvalidator = SInvalidator(new NullInvalidator(this));
    }
    else
    {
        Storage::Config storage_config;
        pStorage->get_config(&storage_config);

        switch (storage_config.invalidate)
        {
        case CACHE_INVALIDATE_NEVER:
            // We must do all invalidation.
            m_sInvalidator = SInvalidator(new FullInvalidator(this));
            break;

        case CACHE_INVALIDATE_CURRENT:
            // We can use the storage for performing invalidation in
            // the storage itself.
            m_sInvalidator = SInvalidator(new StorageInvalidator(this));
        }
    }
}

LRUStorage::~LRUStorage()
{
    do_clear(nullptr);
    delete m_pStorage;
}

bool LRUStorage::create_token(std::shared_ptr<Storage::Token>* psToken)
{
    // The LRUStorage can only be used together with a local storage;
    // one where the cache-communication is not an issue, so we expect
    // the create token to be NULL

    bool rv = m_pStorage->create_token(psToken);
    mxb_assert(!*psToken);

    return rv;
}

void LRUStorage::get_config(Config* pConfig)
{
    *pConfig = m_config;
}

void LRUStorage::get_limits(Limits* pLimits)
{
    m_pStorage->get_limits(pLimits);
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

            json_object_set_new(*ppInfo, "lru", pLru);
        }

        json_t* pStorage_info;

        cache_result_t result = m_pStorage->get_info(what, &pStorage_info);

        if (CACHE_RESULT_IS_OK(result))
        {
            json_object_set_new(*ppInfo, "real_storage", pStorage_info);
        }
    }

    return *ppInfo ? CACHE_RESULT_OK : CACHE_RESULT_OUT_OF_RESOURCES;
}

cache_result_t LRUStorage::do_get_value(Token* pToken,
                                        const CacheKey& key,
                                        uint32_t flags,
                                        uint32_t soft_ttl,
                                        uint32_t hard_ttl,
                                        GWBUF** ppValue)
{
    mxb_assert(!pToken);
    return access_value(APPROACH_GET, key, flags, soft_ttl, hard_ttl, ppValue);
}

cache_result_t LRUStorage::do_put_value(Token* pToken,
                                        const CacheKey& key,
                                        const vector<string>& invalidation_words,
                                        const GWBUF* pValue)
{
    mxb_assert(!pToken);

    cache_result_t result = CACHE_RESULT_ERROR;

    size_t value_size = gwbuf_link_length(pValue);

    Node* pNode = NULL;

    NodesByKey::iterator i = m_nodes_by_key.find(key);
    bool existed = (i != m_nodes_by_key.end());

    if (existed)
    {
        result = get_existing_node(i, pValue, &pNode);
    }
    else
    {
        result = get_new_node(key, pValue, &i, &pNode);
    }

    if (CACHE_RESULT_IS_OK(result))
    {
        mxb_assert(pNode);

        const vector<string>& storage_words = m_sInvalidator->storage_words(invalidation_words);

        result = m_pStorage->put_value(pToken, key, storage_words, pValue);

        if (CACHE_RESULT_IS_OK(result))
        {
            if (existed)
            {
                ++m_stats.updates;
                mxb_assert(m_stats.size >= pNode->size());
                m_stats.size -= pNode->size();
            }
            else
            {
                ++m_stats.items;
            }

            const vector<string>& node_words = m_sInvalidator->node_words(invalidation_words);

            pNode->reset(&i->first, value_size, node_words);
            m_sInvalidator->make_note(pNode);

            m_stats.size += pNode->size();

            move_to_head(pNode);
        }
        else if (!existed)
        {
            MXS_ERROR("Could not put a value to the storage.");
            free_node(i, InvalidatorAction::IGNORE);
        }
    }

    return result;
}

cache_result_t LRUStorage::do_del_value(Token* pToken, const CacheKey& key)
{
    mxb_assert(!pToken);

    cache_result_t result = CACHE_RESULT_NOT_FOUND;

    NodesByKey::iterator i = m_nodes_by_key.find(key);
    bool existed = (i != m_nodes_by_key.end());

    if (existed)
    {
        result = m_pStorage->del_value(pToken, key);

        if (CACHE_RESULT_IS_OK(result) || CACHE_RESULT_IS_NOT_FOUND(result))
        {
            // If it wasn't found, we'll assume it was because ttl has hit in.
            ++m_stats.deletes;

            mxb_assert(m_stats.size >= i->second->size());
            mxb_assert(m_stats.items > 0);

            m_stats.size -= i->second->size();
            --m_stats.items;

            free_node(i, InvalidatorAction::REMOVE);
        }
    }

    return result;
}

cache_result_t LRUStorage::do_invalidate(Token* pToken, const vector<string>& words)
{
    mxb_assert(!pToken);

    cache_result_t rv = CACHE_RESULT_OK;

    if (!m_sInvalidator->invalidate(words))
    {
        string s = mxb::join(words, ",");

        MXS_ERROR("Could not invalidate cache entries dependent upon '%s'."
                  "The entire cache will be cleared.", s.c_str());

        rv = do_clear(pToken);
    }

    return rv;
}

cache_result_t LRUStorage::do_clear(Token* pToken)
{
    mxb_assert(!pToken);

    Node* pnode = m_pHead;

    while (m_pHead)
    {
        free_node(m_pHead, InvalidatorAction::REMOVE); // Adjusts m_pHead
    }

    mxb_assert(!m_pHead);
    mxb_assert(!m_pTail);

    return m_pStorage->clear(pToken);
}

cache_result_t LRUStorage::do_get_head(CacheKey* pKey, GWBUF** ppValue)
{
    cache_result_t result = CACHE_RESULT_NOT_FOUND;

    // Since it's the head it's unlikely to have happened, but we need to loop to
    // cater for the case that ttl has hit in.
    while (m_pHead && (CACHE_RESULT_IS_NOT_FOUND(result)))
    {
        mxb_assert(m_pHead->key());
        result = do_get_value(nullptr,
                              *m_pHead->key(),
                              CACHE_FLAGS_INCLUDE_STALE,
                              CACHE_USE_CONFIG_TTL,
                              CACHE_USE_CONFIG_TTL,
                              ppValue);
    }

    if (CACHE_RESULT_IS_OK(result))
    {
        *pKey = *m_pHead->key();
    }

    return result;
}

cache_result_t LRUStorage::do_get_tail(CacheKey* pKey, GWBUF** ppValue)
{
    cache_result_t result = CACHE_RESULT_NOT_FOUND;

    // We need to loop to cater for the case that ttl has hit in.
    while (m_pTail && CACHE_RESULT_IS_NOT_FOUND(result))
    {
        mxb_assert(m_pTail->key());
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
                                        const CacheKey& key,
                                        uint32_t flags,
                                        uint32_t soft_ttl,
                                        uint32_t hard_ttl,
                                        GWBUF** ppValue)
{
    cache_result_t result = CACHE_RESULT_NOT_FOUND;

    NodesByKey::iterator i = m_nodes_by_key.find(key);
    bool existed = (i != m_nodes_by_key.end());

    if (existed)
    {
        result = m_pStorage->get_value(nullptr, key, flags, soft_ttl, hard_ttl, ppValue, nullptr);

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
                free_node(i, InvalidatorAction::REMOVE);
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
    mxb_assert(m_pTail);

    Node* pNode = NULL;

    if (free_node_data(m_pTail, Context::EVICTION))
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

        if (free_node_data(m_pTail, Context::EVICTION))
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
        pNode->clear();
    }

    return pNode;
}

/**
 * Free the data associated with a node.
 *
 * @param pNode    The node whose data should be freed.
 * @param context  The context where this is done.
 *
 * @return True, if the data could be freed, false otherwise.
 */
bool LRUStorage::free_node_data(Node* pNode, Context context)
{
    bool success = true;

    const CacheKey* pKey = pNode->key();
    mxb_assert(pKey);

    NodesByKey::iterator i = m_nodes_by_key.find(*pKey);

    if (i == m_nodes_by_key.end())
    {
        mxb_assert(!true);
        MXS_ERROR("Item in LRU list was not found in key mapping.");
    }

    cache_result_t result = CACHE_RESULT_OK;

    if (context != Context::LRU_INVALIDATION)
    {
        m_pStorage->del_value(nullptr, *pKey);
    }

    if (CACHE_RESULT_IS_OK(result) || CACHE_RESULT_IS_NOT_FOUND(result))
    {
        if (CACHE_RESULT_IS_NOT_FOUND(result))
        {
            mxb_assert(!true);
            MXS_ERROR("Item in LRU list was not found in storage.");
        }

        if (i != m_nodes_by_key.end())
        {
            m_nodes_by_key.erase(i);
        }

        mxb_assert(m_stats.size >= pNode->size());
        mxb_assert(m_stats.items > 0);

        m_stats.size -= pNode->size();
        m_stats.items -= 1;

        if (context == Context::EVICTION)
        {
            m_stats.evictions += 1;
            m_sInvalidator->remove_note(pNode);
        }
        else
        {
            m_stats.invalidations += 1;
        }
    }
    else
    {
        mxb_assert(!true);
        MXS_ERROR("Could not remove value from storage, cannot "
                  "remove from LRU list or key mapping either.");
        success = false;
    }

    return success;
}

/**
 * Free a node and update head/tail accordingly.
 *
 * @param pNode   The node to be freed.
 * @param action  What to do regarding the invalidator.
 */
void LRUStorage::free_node(Node* pNode, InvalidatorAction action) const
{
    if (action == InvalidatorAction::REMOVE)
    {
        m_sInvalidator->remove_note(pNode);
    }

    remove_node(pNode);
    delete pNode;

    mxb_assert(!m_pHead || (m_pHead->prev() == NULL));
    mxb_assert(!m_pTail || (m_pTail->next() == NULL));
}

/**
 * Free the node referred to by the iterator and update head/tail accordingly.
 *
 * @param i       The map iterator.
 * @param action  What to do regarding the invalidator.
 */
void LRUStorage::free_node(NodesByKey::iterator& i, InvalidatorAction action) const
{
    free_node(i->second, action);   // A Node
    m_nodes_by_key.erase(i);
}

/**
 * Remove a node and update head/tail accordingly.
 *
 * @param pNode  The node to be removed.
 */
void LRUStorage::remove_node(Node* pNode) const
{
    mxb_assert(m_pHead->prev() == NULL);
    mxb_assert(m_pTail->next() == NULL);

    if (m_pHead == pNode)
    {
        m_pHead = m_pHead->next();
    }

    if (m_pTail == pNode)
    {
        m_pTail = m_pTail->prev();
    }

    pNode->remove();

    mxb_assert(!m_pHead || (m_pHead->prev() == NULL));
    mxb_assert(!m_pTail || (m_pTail->next() == NULL));
}

/**
 * Move a node to head.
 *
 * @param pNode  The node to be moved to head.
 */
void LRUStorage::move_to_head(Node* pNode) const
{
    mxb_assert(!m_pHead || (m_pHead->prev() == NULL));
    mxb_assert(!m_pTail || (m_pTail->next() == NULL));

    if (m_pTail == pNode)
    {
        m_pTail = pNode->prev();
    }

    m_pHead = pNode->prepend(m_pHead);

    if (!m_pTail)
    {
        m_pTail = m_pHead;
    }

    mxb_assert(m_pHead);
    mxb_assert(m_pTail);
    mxb_assert((m_pHead != m_pTail) || (m_pHead == pNode));
    mxb_assert(m_pHead->prev() == NULL);
    mxb_assert(m_pTail->next() == NULL);
}

cache_result_t LRUStorage::get_existing_node(NodesByKey::iterator& i, const GWBUF* pValue, Node** ppNode)
{
    cache_result_t result = CACHE_RESULT_OK;

    size_t value_size = gwbuf_link_length(pValue);

    if (value_size > m_max_size)
    {
        // If the size of the new item is more than what is allowed in total,
        // we must remove the value.
        const CacheKey* pKey = i->second->key();
        mxb_assert(pKey);

        result = do_del_value(nullptr, *pKey);

        if (CACHE_RESULT_IS_ERROR(result))
        {
            // Removal of old value of too big a value to be cached failed, we are hosed.
            MXS_ERROR("Value is too big to be stored, and removal of old value "
                      "failed. The cache will return stale data.");
        }

        result = CACHE_RESULT_OUT_OF_RESOURCES;
    }
    else
    {
        Node* pNode = i->second;

        size_t new_size = m_stats.size - pNode->size() + value_size;

        if (new_size > m_max_size)
        {
            mxb_assert(value_size > pNode->size());

            // We move it to the front, so that we do not have to deal with the case
            // that 'pNode' is subject to removal.
            move_to_head(pNode);

            size_t extra_size = value_size - pNode->size();

            Node* pVacant_node = vacate_lru(extra_size);

            if (pVacant_node)
            {
                // We won't be using the node.
                free_node(pVacant_node, InvalidatorAction::IGNORE);

                *ppNode = pNode;
            }
            else
            {
                mxb_assert(!true);
                // If we could not vacant nodes, we are hosed.
                result = CACHE_RESULT_ERROR;
            }
        }
        else
        {
            mxb_assert(m_stats.items <= m_max_count);
            *ppNode = pNode;
        }
    }

    return result;
}

cache_result_t LRUStorage::get_new_node(const CacheKey& key,
                                        const GWBUF* pValue,
                                        NodesByKey::iterator* pI,
                                        Node** ppNode)
{
    cache_result_t result = CACHE_RESULT_OK;

    size_t value_size = gwbuf_link_length(pValue);
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
            mxb_assert(m_stats.items == m_max_count);
            pNode = vacate_lru();
        }
    }
    else
    {
        pNode = new(std::nothrow) Node;
    }

    if (pNode)
    {
        try
        {
            std::pair<NodesByKey::iterator, bool> rv;
            rv = m_nodes_by_key.insert(std::make_pair(key, pNode));
            mxb_assert(rv.second);      // If true, the item was inserted as new (and not updated).
            *pI = rv.first;
        }
        catch (const std::exception& x)
        {
            delete pNode;
            pNode = NULL;
            result = CACHE_RESULT_OUT_OF_RESOURCES;
        }
    }
    else
    {
        result = CACHE_RESULT_ERROR;
    }

    if (CACHE_RESULT_IS_OK(result))
    {
        mxb_assert(pNode);
        *ppNode = pNode;
    }

    return result;
}

bool LRUStorage::invalidate(Node* pNode, Context context)
{
    mxb_assert(context != Context::EVICTION);

    bool rv = free_node_data(pNode, context);

    if (rv)
    {
        // It's the invalidator calling, so it will clean up itself, we should not.
        free_node(pNode, InvalidatorAction::IGNORE);
    }

    return rv;
}

static void set_integer(json_t* pObject, const char* zName, size_t value)
{
    json_t* pValue = json_integer(value);

    if (pValue)
    {
        json_object_set_new(pObject, zName, pValue);
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
    set_integer(pObject, "invalidations", invalidations);
}
