/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/ccdefs.hh>
#include <maxscale/diskspace.hh>
#include <mysql.h>
#include <optional>

namespace maxscale
{

namespace disk
{

using DiskSizeMap = std::map<std::string, disk::SizesAndName>;

/**
 * @brief Get disk space information of a server.
 *
 * The information is obtained by accessing the @c information_schema.disks table,
 * which is available from 10.1.32, 10.2.14 and 10.3.6 onwards.
 *
 * @param pMysql  A valid handle to some server.
 * @return Valid map object if successful.
 *
 * @attn If the function returns a failure and @c mysql_errno(pMysql)
 *       subsequently returns ER_UNKNOWN_TABLE(1109) then either the server
 *       version is too old or the plugin @c DISKS has not been installed.
 */
std::optional<DiskSizeMap> get_info_by_path(MYSQL* pMysql);
}
}
