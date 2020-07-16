/*
 * Copyright (c) 2019 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-07-16
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxbase/ssl.hh>

namespace maxbase
{
SSLConfig::SSLConfig(const std::string& key, const std::string& cert, const std::string& ca)
    : key(key)
    , cert(cert)
    , ca(ca)
{
}

// CA must always be defined for non-empty configurations
bool SSLConfig::empty() const
{
    return ca.empty();
}
}
