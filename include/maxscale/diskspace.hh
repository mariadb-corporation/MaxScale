/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-01-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/ccdefs.hh>
#include <map>
#include <string>
#include <vector>

namespace maxscale
{

namespace disk
{

/**
 * The size information of a particular named disk.
 */
struct SizesAndName
{
    SizesAndName() = default;

    SizesAndName(int64_t total, int64_t used, int64_t available, const std::string& name)
        : total(total)
        , used(used)
        , available(available)
        , name(name)
    {
    }

    int64_t total {0};      /**< The total size of the disk in bytes. */
    int64_t used {0};       /**< The used amount of space of a disk. */

    /**
     * The available amount of space to non-root users. As the reported size is what is available to
     * non-root users, @c available may be smaller than @total - @used.
     */
    int64_t     available {0};
    std::string name;       /**< The name of the disk. E.g. @c /dev/sda1 */
};
}
}
