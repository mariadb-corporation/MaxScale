/*
 * Copyright (c) 2019 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-04-03
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxbase/ssl.hh>
#include <maxbase/string.hh>
#include <strings.h>
#include <sstream>

namespace maxbase
{
namespace ssl_version
{

std::string to_string(uint32_t version)
{
    if ((version & (SSL_TLS_MAX | TLS10 | TLS11 | TLS12 | TLS13)) == 0)
    {
        return "Unknown";
    }
    else if (version & SSL_TLS_MAX)
    {
        return "MAX";
    }

    std::string rval;

    if (version & TLS10)
    {
        rval += "TLSv1.0";
    }

    if (version & TLS11)
    {
        rval += rval.empty() ? "" : ",";
        rval += "TLSv1.1";
    }

    if (version & TLS12)
    {
        rval += rval.empty() ? "" : ",";
        rval += "TLSv1.2";
    }

    if (version & TLS13)
    {
        rval += rval.empty() ? "" : ",";
        rval += "TLSv1.3";
    }

    return rval;
}
}

std::string SSLConfig::to_string() const
{
    std::ostringstream ss;

    ss << "\tSSL initialized:                     yes\n"
       << "\tSSL method type:                     " << mxb::ssl_version::to_string(version) << "\n"
       << "\tSSL certificate verification depth:  " << verify_depth << "\n"
       << "\tSSL peer verification :              " << (verify_peer ? "true" : "false") << "\n"
       << "\tSSL peer host verification :         " << (verify_host ? "true" : "false") << "\n"
       << "\tSSL certificate:                     " << cert << "\n"
       << "\tSSL key:                             " << key << "\n"
       << "\tSSL CA certificate:                  " << ca << "\n";

    return ss.str();
}
}
