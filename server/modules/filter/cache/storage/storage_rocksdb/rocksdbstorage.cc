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
#include <gwdirs.h>

using std::string;
using std::unique_ptr;

namespace
{

const size_t ROCKSDB_KEY_LENGTH = SHA512_DIGEST_LENGTH;
string u_storageDirectory;

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

cache_result_t RocksDBStorage::getKey(const GWBUF* pQuery, char* pKey)
{
    // ss_dassert(gwbuf_is_contiguous(pQuery));
    const uint8_t* pData = static_cast<const uint8_t*>(GWBUF_DATA(pQuery));
    size_t len = MYSQL_GET_PACKET_LEN(pData) - 1; // Subtract 1 for packet type byte.

    const uint8_t* pSql = &pData[5]; // Symbolic constant for 5?

    memset(pKey, 0, CACHE_KEY_MAXLEN);

    SHA512(pSql, len, reinterpret_cast<unsigned char*>(pKey));

    return CACHE_RESULT_OK;
}

cache_result_t RocksDBStorage::getValue(const char* pKey, GWBUF** ppResult)
{
    rocksdb::Slice key(pKey, ROCKSDB_KEY_LENGTH);
    string value;

    rocksdb::Status status = m_sDb->Get(rocksdb::ReadOptions(), key, &value);

    cache_result_t result = CACHE_RESULT_ERROR;

    switch (status.code())
    {
    case rocksdb::Status::kOk:
        {
            *ppResult = gwbuf_alloc(value.length());

            if (*ppResult)
            {
                memcpy(GWBUF_DATA(*ppResult), value.data(), value.length());

                result = CACHE_RESULT_OK;
            }
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
    // ss_dassert(gwbuf_is_contiguous(pValue));

    rocksdb::Slice key(pKey, ROCKSDB_KEY_LENGTH);
    rocksdb::Slice value(static_cast<const char*>(GWBUF_DATA(pValue)), GWBUF_LENGTH(pValue));

    rocksdb::Status status = m_sDb->Put(rocksdb::WriteOptions(), key, value);

    return status.ok() ? CACHE_RESULT_OK : CACHE_RESULT_ERROR;
}
