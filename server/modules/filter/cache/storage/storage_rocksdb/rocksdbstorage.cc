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
#include <fts.h>
#include <algorithm>
#include <set>
#include <rocksdb/env.h>
#include <maxscale/alloc.h>
#include <maxscale/gwdirs.h>
extern "C"
{
// TODO: Add extern "C" to modutil.h
#include <maxscale/modutil.h>
}
#include <maxscale/query_classifier.h>
#include "rocksdbinternals.h"

using std::for_each;
using std::set;
using std::string;
using std::unique_ptr;


namespace
{

const size_t ROCKSDB_KEY_LENGTH = 2 * SHA512_DIGEST_LENGTH;

#if ROCKSDB_KEY_LENGTH > CACHE_KEY_MAXLEN
#error storage_rocksdb key is too long.
#endif

// See https://github.com/facebook/rocksdb/wiki/Basic-Operations#thread-pools
// These figures should perhaps depend upon the number of cache instances.
const size_t ROCKSDB_N_LOW_THREADS = 2;
const size_t ROCKSDB_N_HIGH_THREADS = 1;

struct StorageRocksDBVersion
{
    uint8_t major;
    uint8_t minor;
    uint8_t correction;
};

const uint8_t STORAGE_ROCKSDB_MAJOR = 0;
const uint8_t STORAGE_ROCKSDB_MINOR = 1;
const uint8_t STORAGE_ROCKSDB_CORRECTION = 0;

const StorageRocksDBVersion STORAGE_ROCKSDB_VERSION =
{
    STORAGE_ROCKSDB_MAJOR,
    STORAGE_ROCKSDB_MINOR,
    STORAGE_ROCKSDB_CORRECTION
};

string toString(const StorageRocksDBVersion& version)
{
    string rv;

    rv += "{ ";
    rv += std::to_string(version.major);
    rv += ", ";
    rv += std::to_string(version.minor);
    rv += ", ";
    rv += std::to_string(version.correction);
    rv += " }";

    return rv;
}

const char STORAGE_ROCKSDB_VERSION_KEY[] = "MaxScale_Storage_RocksDB_Version";

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

        if (pFts) {
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

            if (pFts) {
                fts_close(pFts);
            }
        }
    }

    return rv;
}

}

//private
rocksdb::WriteOptions RocksDBStorage::s_writeOptions;

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
    auto pEnv = rocksdb::Env::Default();
    pEnv->SetBackgroundThreads(ROCKSDB_N_LOW_THREADS, rocksdb::Env::LOW);
    pEnv->SetBackgroundThreads(ROCKSDB_N_HIGH_THREADS, rocksdb::Env::HIGH);

    // No logging; the database will always be deleted at startup, so there's
    // no reason for usinf space and processing for writing the write ahead log.
    s_writeOptions.disableWAL = true;

    return true;
}

//static
RocksDBStorage* RocksDBStorage::Create(const char* zName, uint32_t ttl, int argc, char* argv[])
{
    ss_dassert(zName);

    string storageDirectory = get_cachedir();

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
        else
        {
            MXS_WARNING("Unknown argument '%s'.", zKey);
        }
    }

    storageDirectory += "/storage_rocksdb";

    return Create(storageDirectory, zName, ttl);
}

// static
RocksDBStorage* RocksDBStorage::Create(const string& storageDirectory, const char* zName, uint32_t ttl)
{
    RocksDBStorage* pStorage = nullptr;

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
    }
    else
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

            rocksdb::DBWithTTL* pDb;
            rocksdb::Status status;
            rocksdb::Slice key(STORAGE_ROCKSDB_VERSION_KEY);

            status = rocksdb::DBWithTTL::Open(options, path, &pDb, ttl);

            if (status.ok())
            {
                MXS_NOTICE("Database \"%s\" created, storing version %s into it.",
                           path.c_str(), toString(STORAGE_ROCKSDB_VERSION).c_str());

                rocksdb::Slice value(reinterpret_cast<const char*>(&STORAGE_ROCKSDB_VERSION),
                                     sizeof(STORAGE_ROCKSDB_VERSION));

                status = pDb->Put(writeOptions(), key, value);

                if (!status.ok())
                {
                    MXS_ERROR("Could not store version information to created RocksDB database \"%s\". "
                              "You may need to delete the database and retry. RocksDB error: \"%s\"",
                              path.c_str(),
                              status.ToString().c_str());
                }

                unique_ptr<rocksdb::DBWithTTL> sDb(pDb);

                pStorage = new RocksDBStorage(sDb, zName, path, ttl);
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
    }

    return pStorage;
}

cache_result_t RocksDBStorage::getKey(const char* zDefaultDB, const GWBUF* pQuery, CACHE_KEY* pKey)
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

    memset(pKey->data, 0, CACHE_KEY_MAXLEN);

    const unsigned char* pData;

    // We store the databases in the first half of the key. That will ensure that
    // identical queries targeting different default databases will not clash.
    // This will also mean that entries related to the same databases will
    // be placed near each other.
    pData = reinterpret_cast<const unsigned char*>(tag.data());
    SHA512(pData, tag.length(), reinterpret_cast<unsigned char*>(pKey->data));

    char *pSql;
    int length;

    modutil_extract_SQL(const_cast<GWBUF*>(pQuery), &pSql, &length);

    // Then we store the query itself in the second half of the key.
    pData = reinterpret_cast<const unsigned char*>(pSql);
    SHA512(pData, length, reinterpret_cast<unsigned char*>(pKey->data) + SHA512_DIGEST_LENGTH);

    return CACHE_RESULT_OK;
}

cache_result_t RocksDBStorage::getValue(const CACHE_KEY* pKey, uint32_t flags, GWBUF** ppResult)
{
    // Use the root DB so that we get the value *with* the timestamp at the end.
    rocksdb::DB* pDb = m_sDb->GetRootDB();
    rocksdb::Slice key(pKey->data, ROCKSDB_KEY_LENGTH);
    string value;

    rocksdb::Status status = pDb->Get(rocksdb::ReadOptions(), key, &value);

    cache_result_t result = CACHE_RESULT_ERROR;

    switch (status.code())
    {
    case rocksdb::Status::kOk:
        if (value.length() >= RocksDBInternals::TS_LENGTH)
        {
            bool isStale = RocksDBInternals::IsStale(value, m_ttl, rocksdb::Env::Default());

            if (!isStale || ((flags & CACHE_FLAGS_INCLUDE_STALE) != 0))
            {
                size_t length = value.length() - RocksDBInternals::TS_LENGTH;

                *ppResult = gwbuf_alloc(length);

                if (*ppResult)
                {
                    memcpy(GWBUF_DATA(*ppResult), value.data(), length);

                    if (isStale)
                    {
                        result = CACHE_RESULT_STALE;
                    }
                    else
                    {
                        result = CACHE_RESULT_OK;
                    }
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

cache_result_t RocksDBStorage::putValue(const CACHE_KEY* pKey, const GWBUF* pValue)
{
    ss_dassert(GWBUF_IS_CONTIGUOUS(pValue));

    rocksdb::Slice key(pKey->data, ROCKSDB_KEY_LENGTH);
    rocksdb::Slice value((char*)GWBUF_DATA(pValue), GWBUF_LENGTH(pValue));

    rocksdb::Status status = m_sDb->Put(writeOptions(), key, value);

    return status.ok() ? CACHE_RESULT_OK : CACHE_RESULT_ERROR;
}

cache_result_t RocksDBStorage::delValue(const CACHE_KEY* pKey)
{
    ss_dassert(pKey);

    rocksdb::Slice key(pKey->data, ROCKSDB_KEY_LENGTH);

    rocksdb::Status status = m_sDb->Delete(writeOptions(), key);

    return status.ok() ? CACHE_RESULT_OK : CACHE_RESULT_ERROR;
}
