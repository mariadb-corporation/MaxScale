#include "rewritesql.hh"
#include <iostream>
#include <sstream>
#include <numeric>
#include <memory>

using namespace std::string_literals;

namespace
{
void write_regex_char(std::string* str, char ch)
{
    if (ch == '(' || ch == ')')
    {
        *str += '\\';
    }
    *str += ch;
}
}

RewriteSql::RewriteSql(const std::string& match_template,
                       const std::string& replace_template)
    : m_regex_template(match_template)
    , m_replace_template(replace_template)
    , m_replacer(replace_template)
{
    std::ostringstream error_stream;

    // TODO: allow the same placeholder multiple times. This is the same
    //       as forward references and is easy to do without changes to the
    //       simple regex with groups, just ordinal numbers which are
    //       currently implied (1,2,3,...).
    if (!m_replacer.is_valid())
    {
        error_stream << m_replacer.error_str();
        goto end_constructor;
    }
    else
    {
        auto last = end(m_regex_template);
        auto ite = begin(m_regex_template);

        while (ite != last)
        {
            switch (*ite)
            {
            case '\\':
                {
                    m_regex_str += *ite;
                    if (ite + 1 != last)
                    {
                        m_regex_str += *++ite;
                    }
                }
                break;

            case PLACEHOLDER_CHAR:
                {
                    ++m_nreplacements;
                    auto ite_before = ite - 1;      // for error output

                    size_t n{};
                    ite = read_placeholder(ite, last, &n);

                    if (n == 0)
                    {
                        error_stream << "Invalid placeholder at: " << std::string(ite_before, last);
                        goto end_constructor;
                    }

                    if (n != m_nreplacements)
                    {
                        error_stream << "Placeholders must be numbered 1,2,3,... error at => "
                                     << std::string(ite_before, last);
                        goto end_constructor;
                    }

                    const std::string group = "(.*)";

                    m_regex_str += group;

                    if (ite != last)
                    {
                        write_regex_char(&m_regex_str, *ite);
                    }
                }
                break;

            default:
                write_regex_char(&m_regex_str, *ite);
                break;
            }

            if (ite != last)
            {
                ++ite;
            }
        }

        if (m_nreplacements == 0)
        {
            error_stream << "No replacements (@{1}, @{2}, ...) found in regex_template";
            goto end_constructor;
        }
    }

    // TODO more error checking here: make sure the match_template and replace_template
    // work together (in effect that the max ordinal in the replament is <= to that in the match)

end_constructor:
    m_error_str = error_stream.str();

    if (m_error_str.empty())
    {
        try
        {
            m_regex = std::regex{m_regex_str};
        }
        catch (const std::exception& ex)
        {
            m_error_str = ex.what();
        }
    }
}

bool RewriteSql::replace(const std::string& sql, std::string* pSql) const
{
    std::smatch match;
    bool matched = std::regex_search(sql, match, m_regex);

    if (!matched || match.size() != m_nreplacements + 1)
    {
        return false;
    }

    std::vector<std::string> replacements;

    for (size_t i = 1; i < match.size(); ++i)
    {
        replacements.emplace_back(match[i]);
    }

    *pSql = m_replacer.replace(replacements);

    return true;
}
