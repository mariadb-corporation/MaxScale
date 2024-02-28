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
#pragma once

#include <maxbase/ccdefs.hh>
#include <maxbase/exception.hh>
#include <vector>
#include <regex>

DEFINE_EXCEPTION(RewriteError);

// Make the std::regex_constants that select a regex grammar an enum
// *uncrustify-off*
enum class RegexGrammar : int64_t
{
    Native,      // The native regex filter syntax, e.g @{1}
    ECMAScript,  // https://en.cppreference.com/w/cpp/regex/ecmascript
    Posix,       // basic Posix http://pubs.opengroup.org/onlinepubs/9699919799/basedefs/V1_chap09.html#tag_09_03
    EPosix,      // extended Posix http://pubs.opengroup.org/onlinepubs/9699919799/basedefs/V1_chap09.html#tag_09_04
    Awk,         // awk http://pubs.opengroup.org/onlinepubs/9699919799/utilities/awk.html#tag_20_06_13_04
    Grep,        // same as Posix with the addition of newline '\n' as an alternation separator.
    EGrep,       // same as EPosix with the addition of newline '\n' as an alternation separator in addition to '|'.
    END
};
// *uncrustify-on*

std::regex_constants::syntax_option_type to_regex_grammar_flag(RegexGrammar type);

std::string  valid_grammar_values();
RegexGrammar grammar_from_string(const std::string& str);

struct TemplateDef
{
    bool         case_sensitive = true;
    RegexGrammar regex_grammar = RegexGrammar::Native;
    bool         what_if = false;
    bool         continue_if_matched = false;
    bool         ignore_whitespace = true;
    std::string  match_template;
    std::string  replace_template;

    std::vector<std::string> unit_test_input;
    std::vector<std::string> unit_test_output;
};

// Could be a free function but wrapped for extensions
class TemplateReader
{
public:
    TemplateReader(const std::string& template_file, const TemplateDef& dfault);
    std::vector<TemplateDef> templates() const;
private:
    std::string m_path;
    TemplateDef m_default_template;
};

// Throws if TemplateDef is not complete
void validate_template_def(const TemplateDef& def);
