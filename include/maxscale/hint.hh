/*
 * Copyright (c) 2018 MariaDB Corporation Ab
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
#pragma once

#include <maxscale/ccdefs.hh>

/**
 * A routing hint has a type and may optionally have hint-specific data.
 */
struct Hint
{
    enum class Type
    {
        NONE,
        ROUTE_TO_MASTER,
        ROUTE_TO_SLAVE,
        ROUTE_TO_NAMED_SERVER,
        ROUTE_TO_UPTODATE_SERVER,   /**< not supported by RWSplit and HintRouter */
        ROUTE_TO_ALL,               /**< not supported by RWSplit, supported by HintRouter */
        ROUTE_TO_LAST_USED,
        PARAMETER,
    };

    static const char* type_to_str(Type t);

    Hint() = default;
    explicit Hint(Type type);

    /**
     * Create a hint with data. Typically used for ROUTE_TO_NAMED_SERVER-hints.
     *
     * @param type Hint type
     * @param data Data
     */
    Hint(Type type, std::string data);

    /**
     * Create a parameter-type hint.
     *
     * @param param_name Parameter name
     * @param param_value Parameter value
     */
    Hint(std::string param_name, std::string param_value);

    /**
     * Is the hint valid?
     *
     * @return True if hint type is valid, i.e not "NONE".
     */
    explicit operator bool() const;

    Type        type {Type::NONE};  /**< The type of hint */
    std::string data;               /**< Data or parameter name */
    std::string value;              /**< Parameter value */
};
