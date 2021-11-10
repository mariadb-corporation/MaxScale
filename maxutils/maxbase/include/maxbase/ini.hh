/*
 * Copyright (c) 2021 MariaDB Corporation Ab
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

#include <maxbase/ccdefs.hh>
#include <map>
#include <string>
#include <vector>

namespace maxbase
{
namespace ini
{
using StringVector = std::vector<std::string>;

// The types in the array_result-namespace contains parse results in array form with minimal
// processing or checking.
namespace array_result
{
struct ValueDef
{
    std::string name;
    std::string value;
    int         lineno {-1};

    ValueDef(std::string name, std::string value, int lineno = -1);
};

struct ConfigSection
{
    std::string           header;
    std::vector<ValueDef> key_values;
    int                   lineno {-1};
};

using Configuration = std::vector<ConfigSection>;

struct ParseResult
{
    bool success {false};
    int  err_lineno {-1};

    Configuration sections;
};
}

namespace map_result
{
// The types in this namespace contain parse results in map form. Section and key names are unique and
// not empty. Other than that, no checking is done.

struct ValueDef
{
    std::string value;
    int         lineno {-1};

    explicit ValueDef(std::string value, int lineno = -1);
};

struct ConfigSection
{
    std::map<std::string, ValueDef> key_values;
    int                             lineno{-1};
};

using Configuration = std::map<std::string, ConfigSection>;

struct ParseResult
{
    Configuration config;
    StringVector  errors;
};

ParseResult convert_to_map(mxb::ini::array_result::Configuration&& config_in);
}

// This should match the type expected by inih. The type can change depending on compilation settings
// so best define it here and hide the library type.
using IniHandler = int (*)(void* userdata, const char* section, const char* name, const char* value,
                           int lineno);

/**
 * Calls ini_parse.
 */
int parse_file(const char* filename, IniHandler handler, void* userdata);

array_result::ParseResult parse_config_text(const std::string& config_text);
map_result::ParseResult   parse_config_text_to_map(const std::string& config_text);
map_result::ParseResult   parse_config_file_to_map(const std::string& config_file);
StringVector              substitute_env_vars(map_result::Configuration& config);
std::string               config_map_to_string(const map_result::Configuration& input);
}
}
