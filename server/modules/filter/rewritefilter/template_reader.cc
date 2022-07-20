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
