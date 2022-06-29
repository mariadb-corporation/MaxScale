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

TemplateReader::TemplateReader(const std::string& template_file)
    : m_path(template_file)
{
}

std::vector<TemplateDef> TemplateReader::templates() const
{
    std::vector<TemplateDef> ret;
    Json json;
    if (json.load(m_path))
    {
        auto arr = json.get_array_elems("templates");
        for (auto& t : arr)
        {
            TemplateDef def;
            def.match_template = t.get_string("match_template");
            def.replace_template = t.get_string("replace_template");

            ret.push_back(std::move(def));
        }
    }
    else
    {
        MXB_SERROR("Failed to load rewrite template file: "
                   << m_path
                   << " error: "
                   << json.error_msg().c_str());
    }

    return ret;
}
