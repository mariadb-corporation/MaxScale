/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2020-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/mariadb.hh>
#include <maxscale/debug.h>

namespace
{

using namespace maxscale;

typedef void (*Callback)(void*       pCollection,
                         const char* zDisk,
                         const char* zPath,
                         int64_t     total,
                         int64_t     used,
                         int64_t     available);

int get_info(MYSQL* pMysql, Callback pCallback, void* pCollection)
{
    int rv = 0;

    rv = mysql_query(pMysql, "SELECT Disk, Path, Total, Used, Available FROM information_schema.disks");

    if (rv == 0)
    {
        MYSQL_RES* pResult = mysql_store_result(pMysql);

        if (pResult)
        {
            ss_dassert(mysql_field_count(pMysql) == 5);

            MYSQL_ROW row;

            while ((row = mysql_fetch_row(pResult)) != NULL)
            {
                char* pEnd;

                int64_t total = strtoll(row[2], &pEnd, 0);
                ss_dassert(*pEnd == 0);
                int64_t used = strtoll(row[3], &pEnd, 0);
                ss_dassert(*pEnd == 0);
                int64_t available = strtoll(row[4], &pEnd, 0);
                ss_dassert(*pEnd == 0);

                pCallback(pCollection, row[0], row[1], total, used, available);
            }

            mysql_free_result(pResult);
        }
    }

    return rv;
}

template<class Collection>
inline int get_info(MYSQL* pMysql,
                    void (*pCallback)(Collection* pCollection,
                                      const char* zDisk,
                                      const char* zPath,
                                      int64_t     total,
                                      int64_t     used,
                                      int64_t     available),
                    Collection* pCollection)
{
    pCollection->clear();

    return get_info(pMysql, reinterpret_cast<Callback>(pCallback), pCollection);
}

void add_info_by_path(std::map<std::string, disk::SizesAndName>* pSizes,
                      const char* zDisk,
                      const char* zPath,
                      int64_t     total,
                      int64_t     used,
                      int64_t     available)
{
    pSizes->insert(std::make_pair(zPath, disk::SizesAndName(total, used, available, zDisk)));
}

void add_info_by_disk(std::map<std::string, disk::SizesAndPaths>* pSizes,
                      const char* zDisk,
                      const char* zPath,
                      int64_t     total,
                      int64_t     used,
                      int64_t     available)
{
    auto i = pSizes->find(zDisk);

    if (i == pSizes->end())
    {
        disk::SizesAndPaths& item = i->second;

        ss_dassert(total == item.total());
        ss_dassert(used == item.used());
        ss_dassert(available == item.available());

        item.add_path(zPath);
    }
    else
    {
        disk::SizesAndPaths item(total, used, available, zPath);

        pSizes->insert(std::make_pair(zDisk, item));
    }
}

}


namespace maxscale
{

namespace disk
{

int get_info_by_path(MYSQL* pMysql, std::map<std::string, disk::SizesAndName>* pInfo)
{
    return get_info(pMysql, add_info_by_path, pInfo);
}

int get_info_by_disk(MYSQL* pMysql, std::map<std::string, disk::SizesAndPaths>* pInfo)
{
    return get_info(pMysql, add_info_by_disk, pInfo);
}

}

}
