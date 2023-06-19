/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-03-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <string>
#include <variant>

struct S3URL
{
    std::string bucket;
    std::string filename;
};

struct LoadDataResult
{
    bool        local;
    S3URL       s3;
    std::string db;
    std::string table;
    std::string remaining_sql;
};

struct ParseError
{
    std::string message;
};

std::variant<LoadDataResult, ParseError> parse_ldi(std::string_view);
