/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-04-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/protocol/mariadb/diskspace.hh>
#include <maxbase/assert.hh>

namespace maxscale
{
namespace disk
{

std::optional<DiskSizeMap> get_info_by_path(MYSQL* pMysql)
{
    std::optional<DiskSizeMap> rval;
    int rv = mysql_query(pMysql, "SELECT Disk, Path, Total, Used, Available FROM information_schema.disks");
    if (rv == 0)
    {
        DiskSizeMap sizes;
        MYSQL_RES* pResult = mysql_store_result(pMysql);

        if (pResult)
        {
            mxb_assert(mysql_field_count(pMysql) == 5);

            MYSQL_ROW row;

            while ((row = mysql_fetch_row(pResult)) != NULL)
            {
                char* pEnd;

                int64_t total = strtoll(row[2], &pEnd, 0);
                mxb_assert(*pEnd == 0);
                int64_t used = strtoll(row[3], &pEnd, 0);
                mxb_assert(*pEnd == 0);
                int64_t available = strtoll(row[4], &pEnd, 0);
                mxb_assert(*pEnd == 0);

                const char* zDisk = row[0];
                const char* zPath = row[1];
                sizes.insert(
                    std::make_pair(zPath, mxs::disk::SizesAndName(total, used, available, zDisk)));
            }

            mysql_free_result(pResult);
        }
        rval = std::move(sizes);
    }

    return rval;
}
}
}
