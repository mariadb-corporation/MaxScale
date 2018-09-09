/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2022-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/ccdefs.hh>
#include <mysql.h>
#include <map>
#include <string>
#include <vector>

namespace maxscale
{

namespace disk
{

/**
 * The size information of a particular disk.
 */
class Sizes
{
public:
    Sizes()
        : m_total(0)
        , m_used(0)
        , m_available(0)
    {
    }

    Sizes(int64_t total,
          int64_t used,
          int64_t available)
        : m_total(total)
        , m_used(used)
        , m_available(available)
    {
    }

    /**
     * The total size of a disk.
     *
     * @return The total size of the disk in bytes.
     */
    int64_t total() const
    {
        return m_total;
    }

    /**
     * The used amount of space of a disk.
     *
     * @return The size of the used amount of space of the disk in bytes.
     */
    int64_t used() const
    {
        return m_used;
    }

    /**
     * The available amount of space to non-root users.
     *
     * @attn As the reported size is what is available to non-root users,
     *       @c available may be smaller than @total - @used.
     *
     * @return The size of the available amount of space of the disk in bytes.
     */
    int64_t available() const
    {
        return m_available;
    }

private:
    int64_t m_total;
    int64_t m_used;
    int64_t m_available;
};

/**
 * The size information of a particular named disk.
 */
class SizesAndName : public Sizes
{
public:
    SizesAndName()
    {
    }

    SizesAndName(int64_t total,
                 int64_t used,
                 int64_t available,
                 const std::string& name)
        : Sizes(total, used, available)
        , m_name(name)
    {
    }

    /**
     * @return The name of the disk. E.g. @c /dev/sda1
     */
    const std::string& name() const
    {
        return m_name;
    }

private:
    std::string m_name;
};

/**
 * The size information of a particular disk, and the paths
 * on which that disk has been mounted.
 */
class SizesAndPaths : public Sizes
{
public:
    SizesAndPaths()
    {
    }

    SizesAndPaths(int64_t total,
                  int64_t used,
                  int64_t available,
                  const std::string& path)
        : Sizes(total, used, available)
    {
        m_paths.push_back(path);
    }

    /**
     * @return The paths that referring to the disk for which the size is reported.
     */
    const std::vector<std::string>& paths() const
    {
        return m_paths;
    }

    void add_path(const std::string path)
    {
        m_paths.push_back(path);
    }

private:
    int64_t                  m_total;
    int64_t                  m_used;
    int64_t                  m_available;
    std::vector<std::string> m_paths;
};


/**
 * @brief Get disk space information of a server.
 *
 * The information is obtained by accessing the @c information_schema.disks table,
 * which is available from 10.1.32, 10.2.14 and 10.3.6 onwards.
 *
 * @param pMysql  A valid handle to some server.
 * @param pInfo   [out] Filled with disk space information, ordered by path.
 *
 * @return 0 if successful.
 *
 * @attn If the function returns a non-zero value and @c mysql_errno(pMysql)
 *       subsequently returns ER_UNKNOWN_TABLE(1109) then either the server
 *       version is too old or the plugin @c DISKS has not been installed.
 */
int get_info_by_path(MYSQL* pMysql, std::map<std::string, disk::SizesAndName>* pInfo);

/**
 * @brief Get disk space information of a server.
 *
 * The information is obtained by accessing the @c information_schema.disks table,
 * which is available from 10.1.32, 10.2.14 and 10.3.6 onwards.
 *
 * @param pMysql  A valid handle to some server.
 * @param pInfo   [out] Filled with disk space information, ordered by disk.
 *
 * @return 0 if successful.
 *
 * @attn If the function returns a non-zero value and @c mysql_errno(pMysql)
 *       subsequently returns ER_UNKNOWN_TABLE(1109) then either the server
 *       version is too old or the plugin @c DISKS has not been installed.
 */
int get_info_by_disk(MYSQL* pMysql, std::map<std::string, disk::SizesAndPaths>* pInfo);
}
}
