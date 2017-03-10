/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2019-07-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#define MXS_MODULE_NAME "storage_rocksdb"
#include "rocksdbstorage.hh"
#include <sys/stat.h>
#include <sys/types.h>
#include <fts.h>
#include <algorithm>
#include <rocksdb/env.h>
#include <rocksdb/statistics.h>
#include <maxscale/alloc.h>
#include <maxscale/paths.h>
#include <maxscale/modutil.h>
#include <maxscale/query_classifier.h>
#include "rocksdbinternals.hh"

using std::for_each;
using std::string;
using std::unique_ptr;


namespace
{

// See https://github.com/facebook/rocksdb/wiki/Basic-Operations#thread-pools
// These figures should perhaps depend upon the number of cache instances.
const size_t ROCKSDB_N_LOW_THREADS = 2;
const size_t ROCKSDB_N_HIGH_THREADS = 1;

/**
 * Deletes a path, irrespective of whether it represents a file, a directory
 * or a directory hierarchy. If the path does not exist, then the path is
 * considered to have been removed.
 *
 * @param path A path (file or directory).
 *
 * @return True if the path could be deleted, false otherwise.
 */
bool deletePath(const string& path)
{
    int rv = false;

    struct stat st;

    if (stat(path.c_str(), &st) == -1)
    {
        if (errno == ENOENT)
        {
            rv = true;
        }
        else
        {
            char errbuf[MXS_STRERROR_BUFLEN];
            MXS_ERROR("Could not stat: %s", strerror_r(errno, errbuf, sizeof(errbuf)));
        }
    }
    else
    {
        MXS_NOTICE("Deleting cache storage at '%s'.", path.c_str());

        rv = true;

        char* files[] = { (char *) path.c_str(), NULL };

        // FTS_NOCHDIR  - Do not change CWD while traversing.
        // FTS_PHYSICAL - Don't follow symlinks.
        // FTS_XDEV     - Don't cross filesystem boundaries
        FTS *pFts = fts_open(files, FTS_NOCHDIR | FTS_PHYSICAL | FTS_XDEV, NULL);

        if (pFts)
        {
            FTSENT* pCurrent;
            while ((pCurrent = fts_read(pFts)))
            {
                switch (pCurrent->fts_info)
                {
                case FTS_NS:
                case FTS_DNR:
                case FTS_ERR:
                    {
                        char errbuf[MXS_STRERROR_BUFLEN];
                        MXS_ERROR("Error while traversing %s: %s",
                                  pCurrent->fts_accpath,
                                  strerror_r(pCurrent->fts_errno, errbuf, sizeof(errbuf)));
                        rv = false;
                    }
                    break;

                case FTS_DC:
                case FTS_DOT:
                case FTS_NSOK:
                    // Not reached unless FTS_LOGICAL, FTS_SEEDOT, or FTS_NOSTAT were
                    // passed to fts_open()
                    break;

                case FTS_D:
                    // Do nothing. Need depth-first search, so directories are deleted
                    // in FTS_DP
                    break;

                case FTS_DP:
                case FTS_F:
                case FTS_SL:
                case FTS_SLNONE:
                case FTS_DEFAULT:
                    if (remove(pCurrent->fts_accpath) < 0)
                    {
                        char errbuf[MXS_STRERROR_BUFLEN];
                        MXS_ERROR("Could not remove '%s', the cache directory may need to "
                                  "be deleted manually: %s",
                                  pCurrent->fts_accpath,
                                  strerror_r(errno, errbuf, sizeof(errbuf)));
                        rv = false;
                    }
                    break;

                default:
                    ss_dassert(!true);
                }
            }

            if (rv)
            {
                MXS_NOTICE("Deleted cache storage at '%s'.", path.c_str());
            }

            if (pFts)
            {
                fts_close(pFts);
            }
        }
    }

    return rv;
}

}

//private
rocksdb::WriteOptions RocksDBStorage::s_write_options;

//private
RocksDBStorage::RocksDBStorage(const string& name,
                               const CACHE_STORAGE_CONFIG& config,
                               const string& path,
                               unique_ptr<rocksdb::DBWithTTL>& sDb)
    : m_name(name)
    , m_config(config)
    , m_path(path)
    , m_sDb(std::move(sDb))
{
}

RocksDBStorage::~RocksDBStorage()
{
}

bool RocksDBStorage::Initialize(uint32_t* pCapabilities)
{
    *pCapabilities = CACHE_STORAGE_CAP_MT;

    auto pEnv = rocksdb::Env::Default();
    pEnv->SetBackgroundThreads(ROCKSDB_N_LOW_THREADS, rocksdb::Env::LOW);
    pEnv->SetBackgroundThreads(ROCKSDB_N_HIGH_THREADS, rocksdb::Env::HIGH);

    // No logging; the database will always be deleted at startup, so there's
    // no reason for usinf space and processing for writing the write ahead log.
    s_write_options.disableWAL = true;

    return true;
}

RocksDBStorage* RocksDBStorage::Create_instance(const char* zName,
                                                const CACHE_STORAGE_CONFIG& config,
                                                int argc, char* argv[])
{
    ss_dassert(zName);

    string storageDirectory = get_cachedir();
    bool collectStatistics = false;

    for (int i = 0; i < argc; ++i)
    {
        size_t len = strlen(argv[i]);
        char arg[len + 1];
        strcpy(arg, argv[i]);

        const char* zValue = NULL;
        char *zEq = strchr(arg, '=');

        if (zEq)
        {
            *zEq = 0;
            zValue = trim(zEq + 1);
        }

        const char* zKey = trim(arg);

        if (strcmp(zKey, "cache_directory") == 0)
        {
            if (zValue)
            {
                storageDirectory = zValue;
            }
            else
            {
                MXS_WARNING("No value specified for '%s', using default '%s' instead.",
                            zKey, get_cachedir());
            }
        }
        else if (strcmp(zKey, "collect_statistics") == 0)
        {
            if (zValue)
            {
                collectStatistics = config_truth_value(zValue);
            }
        }
        else
        {
            MXS_WARNING("Unknown argument '%s'.", zKey);
        }
    }

    storageDirectory += "/storage_rocksdb";

    return Create(zName, config, storageDirectory, collectStatistics);
}

RocksDBStorage* RocksDBStorage::Create(const char* zName,
                                       const CACHE_STORAGE_CONFIG& config,
                                       const string& storageDirectory,
                                       bool collectStatistics)
{
    unique_ptr<RocksDBStorage> sStorage;

    bool ok = true;

    if (mkdir(storageDirectory.c_str(), S_IRWXU) == 0)
    {
        MXS_NOTICE("Created storage directory %s.", storageDirectory.c_str());
    }
    else if (errno != EEXIST)
    {
        char errbuf[MXS_STRERROR_BUFLEN];
        MXS_ERROR("Failed to create storage directory %s: %s",
                  storageDirectory.c_str(),
                  strerror_r(errno, errbuf, sizeof(errbuf)));
        ok = false;
    }

    if (ok)
    {
        string path(storageDirectory + "/" + zName);

        if (deletePath(path))
        {
            rocksdb::Options options;
            options.env = rocksdb::Env::Default();
            options.max_background_compactions = ROCKSDB_N_LOW_THREADS;
            options.max_background_flushes = ROCKSDB_N_HIGH_THREADS;

            options.create_if_missing = true;
            options.error_if_exists = true;

            if (collectStatistics)
            {
                options.statistics = rocksdb::CreateDBStatistics();
            }

            rocksdb::DBWithTTL* pDb;
            rocksdb::Status status;

            status = rocksdb::DBWithTTL::Open(options, path, &pDb, config.hard_ttl);

            if (status.ok())
            {
                unique_ptr<rocksdb::DBWithTTL> sDb(pDb);

                sStorage = unique_ptr<RocksDBStorage>(new RocksDBStorage(zName, config, path, sDb));
            }
            else
            {
                MXS_ERROR("Could not create RocksDB database %s. RocksDB error: \"%s\"",
                          path.c_str(), status.ToString().c_str());

                if (status.IsIOError())
                {
                    MXS_ERROR("Is an other MaxScale process running?");
                }
            }
        }
        else
        {
            MXS_ERROR("Could not delete old storage at %s.", path.c_str());
        }
    }

    return sStorage.release();
}

void RocksDBStorage::get_config(CACHE_STORAGE_CONFIG* pConfig)
{
    *pConfig = m_config;
}

cache_result_t RocksDBStorage::get_info(uint32_t what, json_t** ppInfo) const
{
    json_t* pInfo = json_object();

    if (pInfo)
    {
        auto sStatistics = m_sDb->GetOptions().statistics;

        for_each(rocksdb::TickersNameMap.begin(), rocksdb::TickersNameMap.end(),
                 [pInfo, sStatistics](const std::pair<rocksdb::Tickers, string>& tickerName)
        {
            json_t* pValue = json_integer(sStatistics->getTickerCount(tickerName.first));

            if (pValue)
            {
                json_object_set(pInfo, tickerName.second.c_str(), pValue);
                json_decref(pValue);
            }
        });

        *ppInfo = pInfo;
    }

    return pInfo ? CACHE_RESULT_OK : CACHE_RESULT_OUT_OF_RESOURCES;
}

cache_result_t RocksDBStorage::get_value(const CACHE_KEY& key, uint32_t flags, GWBUF** ppResult)
{
    // Use the root DB so that we get the value *with* the timestamp at the end.
    rocksdb::DB* pDb = m_sDb->GetRootDB();
    rocksdb::Slice rocksdb_key(reinterpret_cast<const char*>(&key.data), sizeof(key.data));
    string value;

    rocksdb::Status status = pDb->Get(rocksdb::ReadOptions(), rocksdb_key, &value);

    cache_result_t result = CACHE_RESULT_ERROR;

    switch (status.code())
    {
    case rocksdb::Status::kOk:
        if (value.length() >= RocksDBInternals::TS_LENGTH)
        {
            rocksdb::Env* pEnv = rocksdb::Env::Default();
            int64_t now;

            if (!pEnv->GetCurrentTime(&now).ok())
            {
                ss_dassert(!true);
                now = INT64_MAX;
            }

            int32_t timestamp = RocksDBInternals::extract_timestamp(value);

            bool is_hard_stale = m_config.hard_ttl == 0 ? false : (now - timestamp > m_config.hard_ttl);
            bool is_soft_stale = m_config.soft_ttl == 0 ? false : (now - timestamp > m_config.soft_ttl);
            bool include_stale = ((flags & CACHE_FLAGS_INCLUDE_STALE) != 0);

            if (is_hard_stale)
            {
                status = m_sDb->Delete(Write_options(), rocksdb_key);

                if (!status.ok())
                {
                    MXS_WARNING("Failed when deleting stale item from RocksDB.");
                }
                result = CACHE_RESULT_NOT_FOUND;
            }
            else if (!is_soft_stale || include_stale)
            {
                size_t length = value.length() - RocksDBInternals::TS_LENGTH;

                *ppResult = gwbuf_alloc(length);

                if (*ppResult)
                {
                    memcpy(GWBUF_DATA(*ppResult), value.data(), length);

                    result = CACHE_RESULT_OK;

                    if (is_soft_stale)
                    {
                        result |= CACHE_RESULT_STALE;
                    }
                }
                else
                {
                    result = CACHE_RESULT_OUT_OF_RESOURCES;
                }
            }
            else
            {
                ss_dassert(is_soft_stale);
                result = (CACHE_RESULT_NOT_FOUND | CACHE_RESULT_STALE);
            }
        }
        else
        {
            MXS_ERROR("RocksDB value too short. Database corrupted?");
            result = CACHE_RESULT_ERROR;
        }
        break;

    case rocksdb::Status::kNotFound:
        result = CACHE_RESULT_NOT_FOUND;
        break;

    default:
        MXS_ERROR("Failed to look up value: %s", status.ToString().c_str());
    }

    return result;
}

cache_result_t RocksDBStorage::put_value(const CACHE_KEY& key, const GWBUF& value)
{
    ss_dassert(GWBUF_IS_CONTIGUOUS(&value));

    rocksdb::Slice rocksdb_key(reinterpret_cast<const char*>(&key.data), sizeof(key.data));
    rocksdb::Slice rocksdb_value((char*)GWBUF_DATA(&value), GWBUF_LENGTH(&value));

    rocksdb::Status status = m_sDb->Put(Write_options(), rocksdb_key, rocksdb_value);

    return status.ok() ? CACHE_RESULT_OK : CACHE_RESULT_ERROR;
}

cache_result_t RocksDBStorage::del_value(const CACHE_KEY& key)
{
    rocksdb::Slice rocksdb_key(reinterpret_cast<const char*>(&key.data), sizeof(key.data));

    rocksdb::Status status = m_sDb->Delete(Write_options(), rocksdb_key);

    return status.ok() ? CACHE_RESULT_OK : CACHE_RESULT_ERROR;
}

cache_result_t RocksDBStorage::get_head(CACHE_KEY* pKey, GWBUF** ppHead) const
{
    return CACHE_RESULT_OUT_OF_RESOURCES;
}

cache_result_t RocksDBStorage::get_tail(CACHE_KEY* pKey, GWBUF** ppHead) const
{
    return CACHE_RESULT_OUT_OF_RESOURCES;
}

cache_result_t RocksDBStorage::get_size(uint64_t* pSize) const
{
    return CACHE_RESULT_OUT_OF_RESOURCES;
}

cache_result_t RocksDBStorage::get_items(uint64_t* pItems) const
{
    return CACHE_RESULT_OUT_OF_RESOURCES;
}
