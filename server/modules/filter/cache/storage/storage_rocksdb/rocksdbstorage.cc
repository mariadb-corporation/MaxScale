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

#include "rocksdbstorage.h"
#include <openssl/sha.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <algorithm>
#include <set>
#include <rocksdb/env.h>
#include <maxscale/alloc.h>
#include <gwdirs.h>
extern "C"
{
// TODO: Add extern "C" to modutil.h
#include <modutil.h>
}
#include <query_classifier.h>
#include "rocksdbinternals.h"

using std::for_each;
using std::set;
using std::string;
using std::unique_ptr;


namespace
{

string u_storageDirectory;

const size_t ROCKSDB_KEY_LENGTH = 2 * SHA512_DIGEST_LENGTH;

#if ROCKSDB_KEY_LENGTH > CACHE_KEY_MAXLEN
#error storage_rocksdb key is too long.
#endif

// See https://github.com/facebook/rocksdb/wiki/Basic-Operations#thread-pools
// These figures should perhaps depend upon the number of cache instances.
const size_t ROCKSDB_N_LOW_THREADS = 2;
const size_t ROCKSDB_N_HIGH_THREADS = 1;

}

//private
RocksDBStorage::RocksDBStorage(unique_ptr<rocksdb::DBWithTTL>& sDb,
                               const string& name,
                               const string& path,
                               uint32_t ttl)
    : m_sDb(std::move(sDb))
    , m_name(name)
    , m_path(path)
    , m_ttl(ttl)
{
}

RocksDBStorage::~RocksDBStorage()
{
}

//static
bool RocksDBStorage::Initialize()
{
    bool initialized = true;

    u_storageDirectory = get_cachedir();
    u_storageDirectory += "/storage_rocksdb";

    if (mkdir(u_storageDirectory.c_str(), S_IRWXU) == 0)
    {
        MXS_NOTICE("Created storage directory %s.", u_storageDirectory.c_str());
    }
    else if (errno != EEXIST)
    {
        initialized = false;
        char errbuf[STRERROR_BUFLEN];

        MXS_ERROR("Failed to create storage directory %s: %s",
                  u_storageDirectory.c_str(),
                  strerror_r(errno, errbuf, sizeof(errbuf)));
    }
    else
    {
        auto pEnv = rocksdb::Env::Default();
        pEnv->SetBackgroundThreads(ROCKSDB_N_LOW_THREADS, rocksdb::Env::LOW);
        pEnv->SetBackgroundThreads(ROCKSDB_N_HIGH_THREADS, rocksdb::Env::HIGH);
    }

    return initialized;
}


//static
RocksDBStorage* RocksDBStorage::Create(const char* zName, uint32_t ttl, int argc, char* argv[])
{
    ss_dassert(zName);

    string path(u_storageDirectory);

    path += "/";
    path += zName;

    rocksdb::Options options;
    options.env = rocksdb::Env::Default();
    options.max_background_compactions = ROCKSDB_N_LOW_THREADS;
    options.max_background_flushes = ROCKSDB_N_HIGH_THREADS;
    options.create_if_missing = true;
    rocksdb::DBWithTTL* pDb;

    rocksdb::Status status = rocksdb::DBWithTTL::Open(options, path, &pDb, ttl);

    RocksDBStorage* pStorage = nullptr;

    if (status.ok())
    {
        unique_ptr<rocksdb::DBWithTTL> sDb(pDb);

        pStorage = new RocksDBStorage(sDb, zName, path, ttl);
    }
    else
    {
        MXS_ERROR("Could not open RocksDB database %s using path %s: %s",
                  zName, path.c_str(), status.ToString().c_str());
    }

    return pStorage;
}

cache_result_t RocksDBStorage::getKey(const char* zDefaultDB, const GWBUF* pQuery, char* pKey)
{
    ss_dassert(GWBUF_IS_CONTIGUOUS(pQuery));

    int n;
    bool fullnames = true;
    char** pzTables = qc_get_table_names(const_cast<GWBUF*>(pQuery), &n, fullnames);

    set<string> dbs; // Elements in set are sorted.

    for (int i = 0; i < n; ++i)
    {
        char *zTable = pzTables[i];
        char *zDot = strchr(zTable, '.');

        if (zDot)
        {
            *zDot = 0;
            dbs.insert(zTable);
        }
        else if (zDefaultDB)
        {
            // If zDefaultDB is NULL, then there will be a table for which we
            // do not know the database. However, that will fail in the server,
            // so nothing will be stored.
            dbs.insert(zDefaultDB);
        }
        MXS_FREE(zTable);
    }
    MXS_FREE(pzTables);

    // dbs now contain each accessed database in sorted order. Now copy them to a single string.
    string tag;
    for_each(dbs.begin(), dbs.end(), [&tag](const string& db) { tag.append(db); });

    memset(pKey, 0, CACHE_KEY_MAXLEN);

    const unsigned char* pData;

    // We store the databases in the first half of the key. That will ensure that
    // identical queries targeting different default databases will not clash.
    // This will also mean that entries related to the same databases will
    // be placed near each other.
    pData = reinterpret_cast<const unsigned char*>(tag.data());
    SHA512(pData, tag.length(), reinterpret_cast<unsigned char*>(pKey));

    char *pSql;
    int length;

    modutil_extract_SQL(const_cast<GWBUF*>(pQuery), &pSql, &length);

    // Then we store the query itself in the second half of the key.
    pData = reinterpret_cast<const unsigned char*>(pSql);
    SHA512(pData, length, reinterpret_cast<unsigned char*>(pKey) + SHA512_DIGEST_LENGTH);

    return CACHE_RESULT_OK;
}

cache_result_t RocksDBStorage::getValue(const char* pKey, GWBUF** ppResult)
{
    // Use the root DB so that we get the value *with* the timestamp at the end.
    rocksdb::DB* pDb = m_sDb->GetRootDB();
    rocksdb::Slice key(pKey, ROCKSDB_KEY_LENGTH);
    string value;

    rocksdb::Status status = pDb->Get(rocksdb::ReadOptions(), key, &value);

    cache_result_t result = CACHE_RESULT_ERROR;

    switch (status.code())
    {
    case rocksdb::Status::kOk:
        if (value.length() >= RocksDBInternals::TS_LENGTH)
        {
            if (!RocksDBInternals::IsStale(value, m_ttl, rocksdb::Env::Default()))
            {
                size_t length = value.length() - RocksDBInternals::TS_LENGTH;

                *ppResult = gwbuf_alloc(length);

                if (*ppResult)
                {
                    memcpy(GWBUF_DATA(*ppResult), value.data(), length);

                    result = CACHE_RESULT_OK;
                }
            }
            else
            {
                MXS_NOTICE("Cache item is stale, not using.");
                result = CACHE_RESULT_NOT_FOUND;
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

cache_result_t RocksDBStorage::putValue(const char* pKey, const GWBUF* pValue)
{
    ss_dassert(GWBUF_IS_CONTIGUOUS(pValue));

    rocksdb::Slice key(pKey, ROCKSDB_KEY_LENGTH);
    rocksdb::Slice value(static_cast<const char*>(GWBUF_DATA(pValue)), GWBUF_LENGTH(pValue));

    rocksdb::Status status = m_sDb->Put(rocksdb::WriteOptions(), key, value);

    return status.ok() ? CACHE_RESULT_OK : CACHE_RESULT_ERROR;
}
