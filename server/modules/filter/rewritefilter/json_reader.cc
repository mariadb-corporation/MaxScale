/*
 * Copyright (c) 2022 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl.
 *
 * Change Date: 2027-11-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "template_reader.hh"
#include <maxbase/json.hh>
#include <maxbase/log.hh>
#include <maxbase/assert.hh>


using mxb::Json;


std::vector<TemplateDef> read_templates_from_json(const std::string& path,
                                                  const TemplateDef& default_def)
{
    std::vector<TemplateDef> ret;
    Json json;

    if (json.load(path))
    {
        auto arr = json.get_array_elems("templates");
        for (auto& t : arr)
        {
            TemplateDef def {default_def};

            bool case_sensitive;
            if (t.try_get_bool("case_sensitive", &case_sensitive))
            {
                def.case_sensitive = case_sensitive;
            }

            std::string regex_grammar_str;
            if (t.try_get_string("regex_grammar", &regex_grammar_str))
            {
                auto grammar = grammar_from_string(regex_grammar_str);
                if (grammar != RegexGrammar::END)
                {
                    def.regex_grammar = grammar;
                }
                else
                {
                    MXB_THROW(RewriteError, "Invalid regex_grammar value `"
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

            bool continue_if_matched;
            if (t.try_get_bool("continue_if_matched", &continue_if_matched))
            {
                def.continue_if_matched = continue_if_matched;
            }

            bool ignore_whitespace;
            if (t.try_get_bool("ignore_whitespace", &ignore_whitespace))
            {
                def.ignore_whitespace = ignore_whitespace;
            }

            def.match_template = t.get_string("match_template");
            if (t.ok())
            {
                def.replace_template = t.get_string("replace_template");
            }

            if (!t.ok())
            {
                MXB_THROW(RewriteError, "Failed to read rewrite template file: "
                          << path
                          << " error: "
                          << t.error_msg().c_str());
                break;
            }

            ret.push_back(std::move(def));
        }
    }
    else
    {
        MXB_THROW(RewriteError, "Failed to load rewrite template file: "
                  << path
                  << " error: "
                  << json.error_msg().c_str());
    }

    return ret;
}
