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
#include "memcachedconfig.hh"
#include <maxscale/cn_strings.hh>

namespace config = mxs::config;

using namespace std;

namespace
{

namespace storage_memcached
{

config::Specification specification(MXB_MODULE_NAME, config::Specification::FILTER);

config::ParamHost host(
    &specification,
    "server",
    "The Memcached server host. Must be of the format 'address[:port]'",
    mxb::Host(MemcachedConfig::DEFAULT_ADDRESS, MemcachedConfig::DEFAULT_PORT),
    MemcachedConfig::DEFAULT_PORT);

config::ParamSize max_value_size(
    &specification,
    "max_value_size",
    "The maximum size of a value.",
    MemcachedConfig::DEFAULT_MAX_VALUE_SIZE);

}

}


MemcachedConfig::MemcachedConfig(const string& name)
    : config::Configuration(name, &storage_memcached::specification)
{
    add_native(&MemcachedConfig::host, &storage_memcached::host);
    add_native(&MemcachedConfig::max_value_size, &storage_memcached::max_value_size);
}

//static
const mxs::config::Specification& MemcachedConfig::specification()
{
    return storage_memcached::specification;
}
