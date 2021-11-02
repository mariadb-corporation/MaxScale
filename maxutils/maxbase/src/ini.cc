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

#include <maxbase/assert.h>
#include <maxbase/ini.hh>
#include <maxbase/format.hh>
#include <maxbase/string.hh>

#define INI_HANDLER_LINENO 1
#include "../../../inih/ini.h"

using std::move;
using std::string;

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
        // If a key without a value was given (e.g. just "dummy" on the line), the value-parameter
        // will be null. Use an empty string in this case. Effectively, the line "dummy" is then equal
        // to "dummy=".
        if (!value)
        {
            value = "";
        }

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

map_result::ParseResult parse_config_text_to_map(const std::string& config_text)
{
    map_result::ParseResult rval;
    auto arr_res = parse_config_text(config_text);
    if (arr_res.success)
    {
        rval = map_result::convert_to_map(move(arr_res.sections));
    }
    else
    {
        string err_msg;
        auto err_lineno = arr_res.err_lineno;
        if (err_lineno > 0)
        {
            // Find the erroneous line. mxb::strtok does not work, as it merges consecutive separators.
            string failed_line;
            int line_ends = 0;
            size_t search_pos = 0;

            while (line_ends < err_lineno - 1)
            {
                search_pos = config_text.find('\n', search_pos);
                line_ends++;
                search_pos++;
            }

            // This assert should apply as long as a syntax error cannot occur on an empty line.
            mxb_assert(search_pos < config_text.length());

            auto line_end_pos = config_text.find('\n', search_pos);
            if (line_end_pos == string::npos)
            {
                failed_line = config_text.substr(search_pos);
            }
            else
            {
                failed_line = config_text.substr(search_pos, line_end_pos - search_pos);
            }

            err_msg = mxb::string_printf("Syntax error at line %i (%s).", err_lineno, failed_line.c_str());
        }
        else
        {
            err_msg = "Parser memory allocation error.";
        }
        rval.errors.push_back(move(err_msg));
    }
    return rval;
}

std::string config_map_to_string(const map_result::Configuration& input)
{
    string rval;
    rval.reserve(2000);

    for (auto& section : input)
    {
        rval.append("[").append(section.first).append("]\n");

        auto& kvs = section.second.key_values;
        for (auto& kv : kvs)
        {
            rval.append(kv.first).append("=").append(kv.second.value).append("\n");
        }
        rval.push_back('\n');
    }
    return rval;
}

namespace map_result
{

ParseResult convert_to_map(ini::array_result::Configuration&& config_in)
{
    StringVector errors;
    Configuration config_out;

    for (auto& section_in : config_in)
    {
        // Duplicate or an empty section name is not allowed.
        if (section_in.header.empty())
        {
            errors.push_back(mxb::string_printf("Section starting at line %i has no name or name "
                                                "is empty.", section_in.lineno));
        }
        else
        {
            auto it_s = config_out.find(section_in.header);
            if (it_s != config_out.end())
            {
                errors.push_back(mxb::string_printf("Section name '%s' at line %i is a duplicate, "
                                                    "previous definition at line %i.",
                                                    section_in.header.c_str(), section_in.lineno,
                                                    it_s->second.lineno));
            }
            else
            {
                // Got a new section. Check that all keys are unique.
                ConfigSection section_out;
                section_out.lineno = section_in.lineno;
                auto& new_kvs = section_out.key_values;

                for (auto& kv_in : section_in.key_values)
                {
                    if (kv_in.name.empty())
                    {
                        errors.push_back(mxb::string_printf(
                                             "Setting starting at line %i in section '%s' has no name.",
                                             kv_in.lineno, section_in.header.c_str()));
                    }
                    else
                    {
                        auto it_kv = new_kvs.find(kv_in.name);
                        if (it_kv != new_kvs.end())
                        {
                            errors.push_back(mxb::string_printf(
                                                 "Setting '%s' in section '%s' at line %i is a duplicate, "
                                                 "previous definition at line %i.",
                                                 kv_in.name.c_str(), section_in.header.c_str(), kv_in.lineno,
                                                 it_kv->second.lineno));
                        }
                        else
                        {
                            ValueDef val_out(move(kv_in.value), kv_in.lineno);
                            section_out.key_values.emplace(move(kv_in.name), move(val_out));
                        }
                    }
                }

                // Section is complete.
                config_out.emplace(move(section_in.header), move(section_out));
            }
        }
    }

    ParseResult rval;
    rval.config = move(config_out);
    rval.errors = move(errors);
    return rval;
}

ValueDef::ValueDef(std::string value, int lineno)
    : value(std::move(value))
    , lineno(lineno)
{
}
}
}
}
