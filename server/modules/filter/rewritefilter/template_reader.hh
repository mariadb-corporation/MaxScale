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
#pragma once

#include <maxbase/ccdefs.hh>
#include <vector>

struct TemplateDef
{
    bool        case_sensitive = true;
    bool        what_if = false;
    std::string match_template;
    std::string replace_template;
};

// Could be a free function but wrapped for extensions
class TemplateReader
{
public:
    TemplateReader(const std::string& template_file, const TemplateDef& dfault);
    std::pair<bool, std::vector<TemplateDef>> templates() const;
private:
    std::string m_path;
    TemplateDef m_default_template;
};
