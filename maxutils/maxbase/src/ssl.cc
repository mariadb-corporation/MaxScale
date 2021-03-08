/*
 * Copyright (c) 2019 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-03-08
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxbase/ssl.hh>
#include <strings.h>
#include <sstream>

namespace maxbase
{
namespace ssl_version
{

const char* to_string(Version version)
{
    switch (version)
    {
    case TLS10:
        return "TLSv10";

    case TLS11:
        return "TLSv11";

    case TLS12:
        return "TLSv12";

    case TLS13:
        return "TLSv13";

    case SSL_MAX:
    case TLS_MAX:
    case SSL_TLS_MAX:
        return "MAX";

    default:
        return "Unknown";
    }
}

Version from_string(const char* str)
{
    if (strcasecmp("MAX", str) == 0)
    {
        return SSL_TLS_MAX;
    }
    else if (strcasecmp("TLSV10", str) == 0)
    {
        return TLS10;
    }
    else if (strcasecmp("TLSV11", str) == 0)
    {
        return TLS11;
    }
    else if (strcasecmp("TLSV12", str) == 0)
    {
        return TLS12;
    }
    else if (strcasecmp("TLSV13", str) == 0)
    {
        return TLS13;
    }
    return SSL_UNKNOWN;
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
