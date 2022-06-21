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

                    m_max_ordinal = std::max(m_max_ordinal, n);
                    m_ordinals.push_back(n - 1);

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

end_constructor:
    m_error_str = error_stream.str();

    if (m_error_str.empty())
    {
        m_error_str = make_ordinals();
    }

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

std::string RewriteSql::make_ordinals()
{
    if (m_replacer.max_placeholder_ordinal() > m_max_ordinal)
    {
        return "The replacement template has larger placeholder numbers than the match template";
    }

    auto ords = m_ordinals;
    std::sort(begin(ords), end(ords));
    ords.erase(std::unique(begin(ords), end(ords)), end(ords));

    std::vector<size_t> monotonical(ords.size());
    std::iota(begin(monotonical), end(monotonical), 0);

    if (monotonical != ords)
    {
        return "The placeholder numbers must be strictly ordered (1,2,3,...)";
    }

    return "";
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
