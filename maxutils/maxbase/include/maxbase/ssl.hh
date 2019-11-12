/*
 * Copyright (c) 2019 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxbase/ccdefs.hh>
#include <string>

namespace maxbase
{
// SSL configuration
struct SSLConfig
{
    SSLConfig() = default;
    SSLConfig(const std::string& key, const std::string& cert, const std::string& ca);
    bool empty() const;

    std::string key;                          /**< SSL private key */
    std::string cert;                         /**< SSL certificate */
    std::string ca;                           /**< SSL CA certificate */
};
}
