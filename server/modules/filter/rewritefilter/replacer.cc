#include "replacer.hh"
#include <sstream>
#include <iostream>


Replacer::Replacer(const std::string& replace_template)
    : m_replace_template(replace_template)
{
    // This does almost the same thing as RewriteSql::RewriteSql() but instead of
    // creating a regex string and regex, it creates a vector of
    // sql parts and placeholders ordinals.
    std::ostringstream error_stream;
    auto last = end(m_replace_template);
    auto ite = begin(m_replace_template);

    std::string current_sql_part;

    while (ite != last)
    {
        switch (*ite)
        {
        case '\\':
            {
                current_sql_part += *ite;
                if (ite + 1 != last)
                {
                    ++ite;
                    current_sql_part += *ite;
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
                    error_stream << "Invalid number at: " << std::string(ite_before, last);
                    goto get_out;
                }

                m_max_placeholder_ordinal = std::max(m_max_placeholder_ordinal, n);

                if (!current_sql_part.empty())
                {
                    m_parts.push_back(current_sql_part);
                    current_sql_part.clear();
                }

                m_parts.push_back(n - 1);   // to zero-based
            }
            break;

        default:
            current_sql_part += *ite++;
            break;
        }
    }

    if (!current_sql_part.empty())
    {
        m_parts.push_back(current_sql_part);
    }

get_out:
    m_error_str = error_stream.str();
}

std::string Replacer::replace(const std::vector<std::string>& replacements) const
{
    std::string sql;
    for (auto part : m_parts)
    {
        if (std::holds_alternative<size_t>(part))
        {
            sql += replacements[std::get<size_t>(part)];
        }
        else
        {
            sql += std::get<std::string>(part);
        }
    }

    return sql;
}
