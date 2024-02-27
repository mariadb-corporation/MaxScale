/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-02-27
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#define MXB_MODULE_NAME "cache"
#include "cachept.hh"

#include <maxbase/atomic.hh>
#include <maxscale/config.hh>
#include <maxscale/mainworker.hh>
#include <maxscale/routingworker.hh>

#include "cachest.hh"
#include "storagefactory.hh"

using std::shared_ptr;
using std::string;
using std::vector;

CachePT::CachePT(const std::string& name,
                 const CacheConfig* pConfig,
                 const CacheRules::SVector& sRules,
                 SStorageFactory sFactory)
    : Cache(name, pConfig, sFactory)
    , m_sRules(sRules)
{
    MXB_NOTICE("Created cache per thread.");
}

CachePT::~CachePT()
{
}

// static
CachePT* CachePT::create(const std::string& name,
                         const CacheRules::SVector& sRules,
                         const CacheConfig* pConfig)
{
    mxb_assert(pConfig);

    CachePT* pCache = NULL;

    StorageFactory* pFactory = NULL;

    if (Cache::get_storage_factory(pConfig, &pFactory))
    {
        shared_ptr<StorageFactory> sFactory(pFactory);

        pCache = new (std::nothrow) CachePT(name, pConfig, sRules, sFactory);
    }

    return pCache;
}

bool CachePT::create_token(std::shared_ptr<Cache::Token>* psToken)
{
    return worker_cache().create_token(psToken);
}

bool CachePT::must_refresh(const CacheKey& key, const CacheFilterSession* pSession)
{
    return worker_cache().must_refresh(key, pSession);
}

void CachePT::refreshed(const CacheKey& key, const CacheFilterSession* pSession)
{
    worker_cache().refreshed(key, pSession);
}

CacheRules::SVector CachePT::all_rules() const
{
    return worker_cache().all_rules();
}

void CachePT::set_all_rules(const CacheRules::SVector& sRules)
{
    mxb_assert(mxs::MainWorker::is_current());

    m_sRules = sRules;

    auto sThis = shared_from_this(); // To ensure that this stays alive during the broadcast.

    mxs::RoutingWorker::broadcast([sThis, sRules](){
            static_cast<CachePT*>(sThis.get())->worker_cache().set_all_rules(sRules);
        }, mxb::Worker::EXECUTE_QUEUED);
}

void CachePT::get_limits(Storage::Limits* pLimits) const
{
    MXB_AT_DEBUG(bool rv=) m_sFactory->get_limits(m_config.storage_params, pLimits);
    mxb_assert(rv);
}

json_t* CachePT::get_info(uint32_t what) const
{
    json_t* pInfo = Cache::do_get_info(what);

    if (pInfo)
    {
        if (what & (INFO_PENDING | INFO_STORAGE))
        {
            what &= ~INFO_RULES;    // The rules are the same, we don't want them duplicated.

            int nRunning = mxs::RoutingWorker::nRunning();
            vector<json_t*> infos(nRunning, nullptr);
            vector<string> keys(nRunning);

            mxs::RoutingWorker::execute_concurrently([this, &infos, &keys, what]() {
                    json_t* pThread_info = worker_cache().get_info(what);

                    if (pThread_info)
                    {
                        unsigned i = mxs::RoutingWorker::get_current()->index();
                        mxb_assert(i < infos.size());

                        char key[20];   // Surely enough.
                        sprintf(key, "thread-%u", i);

                        infos[i] = pThread_info;
                        keys[i] = key;
                    }
                });

            auto it = infos.begin();
            auto jt = keys.begin();

            while (it != infos.end())
            {
                json_t* pThread_info = *it;

                if (pThread_info)
                {
                    json_object_set_new(pInfo, jt->c_str(), pThread_info);
                }

                ++it;
                ++jt;
            }
        }
    }

    return pInfo;
}

cache_result_t CachePT::get_key(const std::string& user,
                                const std::string& host,
                                const char* zDefault_db,
                                const GWBUF& query,
                                CacheKey* pKey) const
{
    return worker_cache().get_key(user, host, zDefault_db, query, pKey);
}

cache_result_t CachePT::get_value(Token* pToken,
                                  const CacheKey& key,
                                  uint32_t flags,
                                  uint32_t soft_ttl,
                                  uint32_t hard_ttl,
                                  GWBUF* pValue,
                                  const std::function<void (cache_result_t, GWBUF&&)>& cb) const
{
    return worker_cache().get_value(pToken, key, flags, soft_ttl, hard_ttl, pValue, cb);
}

cache_result_t CachePT::put_value(Token* pToken,
                                  const CacheKey& key,
                                  const std::vector<std::string>& invalidation_words,
                                  const GWBUF& value,
                                  const std::function<void (cache_result_t)>& cb)
{
    return worker_cache().put_value(pToken, key, invalidation_words, value, cb);
}

cache_result_t CachePT::del_value(Token* pToken,
                                  const CacheKey& key,
                                  const std::function<void (cache_result_t)>& cb)
{
    return worker_cache().del_value(pToken, key, cb);
}

cache_result_t CachePT::invalidate(Token* pToken,
                                   const std::vector<std::string>& words,
                                   const std::function<void (cache_result_t)>& cb)
{
    return worker_cache().invalidate(pToken, words, cb);
}

cache_result_t CachePT::clear(Token* pToken)
{
    return worker_cache().clear(pToken);
}

Cache& CachePT::worker_cache()
{
    SCache& sCache = *m_spWorker_cache;

    if (!sCache)
    {
        string namest(m_name + "-" + std::to_string(mxs::RoutingWorker::get_current()->index()));

        m_mutex.lock();
        auto sRules = m_sRules;
        auto sFactory = m_sFactory;
        m_mutex.unlock();

        MXS_EXCEPTION_GUARD(sCache.reset(CacheST::create(namest, sRules, sFactory, &m_config)));
    }

    return *sCache;
}
