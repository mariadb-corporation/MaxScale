/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-10-11
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#include <maxscale/hint.hh>

const char* STRHINTTYPE(HINT_TYPE t)
{
    const char* rval = nullptr;
    switch (t)
    {
    case HINT_NONE:
        rval = "UNKNOWN HINT TYPE";
        break;

    case HINT_ROUTE_TO_MASTER:
        rval = "HINT_ROUTE_TO_MASTER";
        break;

    case HINT_ROUTE_TO_SLAVE:
        rval = "HINT_ROUTE_TO_SLAVE";
        break;

    case HINT_ROUTE_TO_NAMED_SERVER:
        rval = "HINT_ROUTE_TO_NAMED_SERVER";
        break;

    case HINT_ROUTE_TO_UPTODATE_SERVER:
        rval = "HINT_ROUTE_TO_UPTODATE_SERVER";
        break;

    case HINT_ROUTE_TO_ALL:
        rval = "HINT_ROUTE_TO_ALL";
        break;

    case HINT_ROUTE_TO_LAST_USED:
        rval = "HINT_ROUTE_TO_LAST_USED";
        break;

    case HINT_PARAMETER:
        rval = "HINT_PARAMETER";
        break;
    }
    return rval;
}

HINT::HINT(HINT_TYPE type)
    : type(type)
{
}

HINT::HINT(HINT_TYPE type, std::string data)
    : type(type)
    , data(std::move(data))
{
}

HINT::HINT(std::string param_name, std::string param_value)
    : type(HINT_PARAMETER)
    , data(std::move(param_name))
    , value(std::move(param_value))
{
}

HINT::operator bool() const
{
    return type != HINT_NONE;
}
