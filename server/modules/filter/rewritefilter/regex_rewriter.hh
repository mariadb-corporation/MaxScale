/*
 * Copyright (c) 2022 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl.
 *
 * Change Date: 2027-03-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxbase/ccdefs.hh>
#include "sql_rewriter.hh"

#include <string>
#include <memory>

class NativeReplacer;

/**
 * @brief RegexRewriter uses the match_template for a std::regex
 *        and replaces matches with the replace_template.
 *
 *        C++ regular expressions do not support extended replacement syntax
 *        (essentially placeholders) like pcre2 and boost. Instead, the
 *        matched parts are replaced with a literal replacement.
 *
 *        TODO: Add a class Pcre2Rewriter, if needed. The Native rewriter
 *              can probably handle everything, so this can wait for a
 *              feature request if a problem really cannot be solved
 *              with the NativeRewriter.
 *
 *        TODO: add an option to only replace the first occurrence.
 */
class RegexRewriter : public SqlRewriter
{
public:
    RegexRewriter(const TemplateDef& template_def);

    bool replace(const std::string& sql, std::string* pSql) const override final;

private:
    std::regex m_match_regex;
};
