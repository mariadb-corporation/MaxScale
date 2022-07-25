/*
 * Copyright (c) 2022 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl.
 *
 * Change Date: 2026-06-06
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "template_reader.hh"
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

static_assert(end(grammar_strs) - begin(grammar_strs) == size_t(RegexGrammar::END));

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

std::pair<bool, std::vector<TemplateDef>> TemplateReader::templates() const
{
    std::vector<TemplateDef> ret;
    bool ok = true;
    Json json;

    if (json.load(m_path))
    {
        auto arr = json.get_array_elems("templates");
        for (auto& t : arr)
        {
            TemplateDef def {m_default_template};

            bool case_sensitive;
            if (t.try_get_bool("case_sensitive", &case_sensitive))
            {
                def.case_sensitive = case_sensitive;
            }

            std::string regex_grammar_str;
            if (t.try_get_string("regex_grammar", &regex_grammar_str))
            {
                auto grammar = grammar_from_string(regex_grammar_str);
                ok = grammar != RegexGrammar::END;
                if (ok)
                {
                    def.regex_grammar = grammar;
                }
                else
                {

                    MXB_SERROR("Invalid regex_grammar value `"
                               << regex_grammar_str
                               << "` in rewritefilter template file. "
                               << "Valid values are " << valid_grammar_values()
                               << '\'');
                }
            }

            bool what_if;
            if (t.try_get_bool("what_if", &what_if))
            {
                def.what_if = what_if;
            }

            def.match_template = t.get_string("match_template");
            if (t.ok())
            {
                def.replace_template = t.get_string("replace_template");
            }

            if (!t.ok())
            {
                MXB_SERROR("Failed to read rewrite template file: "
                           << m_path
                           << " error: "
                           << t.error_msg().c_str());
                ok = false;
                break;
            }

            ret.push_back(std::move(def));
        }
    }
    else
    {
        MXB_SERROR("Failed to load rewrite template file: "
                   << m_path
                   << " error: "
                   << json.error_msg().c_str());
        ok = false;
    }

    return {ok, ret};
}
