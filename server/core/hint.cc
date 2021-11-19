/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-11-19
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#include <maxscale/hint.hh>

const char* Hint::type_to_str(Type t)
{
    const char* rval = nullptr;
    switch (t)
    {
    case Type::NONE:
        rval = "UNKNOWN HINT TYPE";
        break;

    case Type::ROUTE_TO_MASTER:
        rval = "HINT_ROUTE_TO_MASTER";
        break;

    case Type::ROUTE_TO_SLAVE:
        rval = "HINT_ROUTE_TO_SLAVE";
        break;

    case Type::ROUTE_TO_NAMED_SERVER:
        rval = "HINT_ROUTE_TO_NAMED_SERVER";
        break;

    case Type::ROUTE_TO_UPTODATE_SERVER:
        rval = "HINT_ROUTE_TO_UPTODATE_SERVER";
        break;

    case Type::ROUTE_TO_ALL:
        rval = "HINT_ROUTE_TO_ALL";
        break;

    case Type::ROUTE_TO_LAST_USED:
        rval = "HINT_ROUTE_TO_LAST_USED";
        break;

    case Type::PARAMETER:
        rval = "HINT_PARAMETER";
        break;
    }
    return rval;
}

Hint::Hint(Type type)
    : type(type)
{
}

Hint::Hint(Type type, std::string data)
    : type(type)
    , data(std::move(data))
{
}

Hint::Hint(std::string param_name, std::string param_value)
    : type(Type::PARAMETER)
    , data(std::move(param_name))
    , value(std::move(param_value))
{
}

Hint::operator bool() const
{
    return type != Type::NONE;
}
