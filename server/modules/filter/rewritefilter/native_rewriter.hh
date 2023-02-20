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
#include "native_replacer.hh"

#include <string>
#include <memory>
#include <queue>

class NativeReplacer;

/**
 * @brief NativeRewriter takes a "match template" where there are placeholders
 * for text that should be replaced in the corresponding "replace template".
 *
 * example:
 * match_template:   "select count(distinct @{1}) from @{2}"
 * replace_template: "select count(*) from (select distinct @{1} from @{2}) as t"
 *
 * TODO: There are very few examples of rewrites, but it is relatively certain that
 *       the distinction between an identifier and a number will be needed:
 *       @{1:s} and @{1:d}. Or 'i' and 'n' if that is clearer to users.
 */
class NativeRewriter : public SqlRewriter
{
public:
    NativeRewriter(const TemplateDef& template_def);

    bool replace(const std::string& sql, std::string* pSql) const override final;

private:
    void make_ordinals();

    std::string m_regex_str;
    std::regex  m_regex;
    size_t      m_nreplacements = 0;

    int m_max_ordinal = 0;

    // An ordinal is the position (ordinal) of the placeholders as they appear
    // in the match template: so @{2}, @{1}, @{2} would lead to m_ordinals
    // containing {1, 0, 1}.
    std::deque<int> m_ordinals;

    // A mapping from an (implied) index to its respective index in m_ordinals.
    // To continue the example above, m_map_ordinals would contain {1, 0},
    // whence m_ordinals[m_map_ordinals[0]] == 1 means that the value
    // of @{1} will be in second regex match group (or actually the third,
    // because the first match group is the entire sql, but that's an
    // implementation detail).
    std::vector<int> m_map_ordinals;

    // Pairs in m_ordinals with the same ordinal (forward reference)
    // Again with the example above, m_match_pairs would have a single
    // element {0,2} reflecting that @{2} appears in the first
    // and third position. For a match those groups have to be the same.
    std::vector<std::pair<int, int>> m_match_pairs;

    NativeReplacer m_replacer;
};
