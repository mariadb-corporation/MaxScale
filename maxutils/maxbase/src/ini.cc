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

#include <maxbase/ini.hh>

#define INI_HANDLER_LINENO 1
#include "../../../inih/ini.h"

using std::move;

namespace
{
/**
 * This handler produces a vector-type configuration parsing result. Minimal string processing.
 * Duplicate headers and settings are allowed. Multiline settings are concatenated.
 */
int standard_handler(void* userdata, const char* section, const char* name,
                     const char* value, int lineno)
{
    using namespace mxb::ini::array_result;
    auto* sections = static_cast<Configuration*>(userdata);
    if (!name && !value)
    {
        // Starting a new section.
        ConfigSection new_section;
        new_section.header = section;
        new_section.lineno = lineno;
        sections->push_back(move(new_section));
    }
    else
    {
        // Got a key-value.
        if (sections->empty())
        {
            // Key-value for a new anonymous section.
            ConfigSection new_section;
            new_section.lineno = lineno;
            new_section.key_values.emplace_back(name, value, lineno);

            sections->push_back(move(new_section));
        }
        else
        {
            // Key-value for an existing section. Can also be a continuation of a multiline value.
            auto& curr_section = sections->back();
            bool is_continuation = !curr_section.key_values.empty()
                && curr_section.key_values.back().name == name;
            if (is_continuation)
            {
                curr_section.key_values.back().value += value;
            }
            else
            {
                curr_section.key_values.emplace_back(name, value, lineno);
            }
        }
    }
    return 1;
}
}

namespace maxbase
{
namespace ini
{
int parse_file(const char* filename, IniHandler handler, void* userdata)
{
    return ::ini_parse(filename, handler, userdata);
}

array_result::ParseResult parse_config_text(const std::string& config_text)
{
    using namespace array_result;
    Configuration sections;
    int rc = ::ini_parse_string(config_text.c_str(), standard_handler, &sections);

    ParseResult rval;
    if (rc == 0)
    {
        // Success.
        rval.success = true;
        rval.sections = std::move(sections);
    }
    else if (rc > 0)
    {
        // Parse error.
        rval.err_lineno = rc;
    }
    // rc < 0 is error as well. Just return false.
    return rval;
}

array_result::ValueDef::ValueDef(std::string name, std::string value, int lineno)
    : name(move(name))
    , value(move(value))
    , lineno(lineno)
{
}
}
}
