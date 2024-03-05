/*
 * Copyright (c) 2022 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl.
 *
 * Change Date: 2028-02-27
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "template_reader.hh"
#include "json_reader.hh"
#include "rf_reader.hh"
#include <maxbase/json.hh>
#include <maxbase/log.hh>
#include <maxbase/assert.hh>

constexpr std::pair<RegexGrammar, const char* const> grammar_strs[]
{
    {RegexGrammar::Native, "Native"},
    {RegexGrammar::ECMAScript, "ECMAScript"},
    {RegexGrammar::Posix, "Posix"},
    {RegexGrammar::EPosix, "EPosix"},
    {RegexGrammar::Awk, "Awk"},
    {RegexGrammar::Grep, "Grep"},
    {RegexGrammar::EGrep, "EGrep"}
};

std::string valid_grammar_values()
{
    std::ostringstream os;
    bool first = true;
    for (auto ite = begin(grammar_strs); ite != end(grammar_strs); ++ite)
    {
        if (ite != begin(grammar_strs))
        {
            os << ", ";
        }
        os << '\'' << ite->second << '\'';
    }

    return os.str();
}

static_assert(end(grammar_strs) - begin(grammar_strs) == size_t(RegexGrammar::END),
              "check grammar_strs[]");

RegexGrammar grammar_from_string(const std::string& str)
{
    auto ite = std::find_if(begin(grammar_strs), end(grammar_strs),
                            [&str](const auto& element) {
        return element.second == str;
    });

    return ite != end(grammar_strs) ? ite->first : RegexGrammar::END;
}

std::regex_constants::syntax_option_type to_regex_grammar_flag(RegexGrammar type)
{
    namespace rx = std::regex_constants;
    rx::syntax_option_type flag{};

    switch (type)
    {
    case RegexGrammar::Native:
    case RegexGrammar::ECMAScript:
        flag = rx::ECMAScript;
        break;

    case RegexGrammar::Posix:
        flag = rx::basic;
        break;

    case RegexGrammar::EPosix:
        flag = rx::extended;
        break;

    case RegexGrammar::Awk:
        flag = rx::awk;
        break;

    case RegexGrammar::Grep:
        flag = rx::grep;
        break;

    case RegexGrammar::EGrep:
        flag = rx::egrep;
        break;

    case RegexGrammar::END:
        mxb_assert(false);
        break;
    }

    return flag;
}

using mxb::Json;

TemplateReader::TemplateReader(const std::string& template_file, const TemplateDef& dfault)
    : m_path(template_file)
    , m_default_template(dfault)
{
}

std::vector<TemplateDef> TemplateReader::templates() const
{
    auto extension_pos = m_path.find_last_of('.');
    if (extension_pos == std::string::npos)
    {
        MXB_THROW(RewriteError, "No extension in: " << m_path);
    }

    auto extension = m_path.substr(extension_pos + 1);
    if (extension == "json")
    {
        return read_templates_from_json(m_path, m_default_template);
    }
    if (extension == "rf")
    {
        return read_templates_from_rf(m_path, m_default_template);
    }

    MXB_THROW(RewriteError, "Unknown extension '" << extension << "' " << m_path
                                                  << ". Valid extensions are json and rf");
}

void validate_template_def(const TemplateDef& def)
{
    if (def.match_template.empty())
    {
        MXB_THROW(RewriteError, "Match template must not be empty");
    }
    if (def.replace_template.empty())
    {
        MXB_THROW(RewriteError, "Replace template must not be empty");
    }
    if (def.unit_test_input.size() != def.unit_test_output.size())
    {
        MXB_THROW(RewriteError, "The number of input/output unit tests must match");
    }
}
