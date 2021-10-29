/*
 * Copyright (c) 2020 MariaDB Corporation Ab
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

#include "nosqlprotocol.hh"
#include <unordered_map>
#include <unordered_set>
#include <bsoncxx/document/element.hpp>
#include <bsoncxx/document/view.hpp>

namespace nosql
{

using UpdateOperatorConverter = std::string (*)(const bsoncxx::document::element& element,
                                                const std::string& doc,
                                                std::unordered_set<std::string>& paths);

extern std::unordered_map<std::string, UpdateOperatorConverter> update_operator_converters;

std::string convert_update_operations(const bsoncxx::document::view& update_operations);

}

