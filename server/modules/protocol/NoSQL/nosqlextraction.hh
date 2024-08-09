/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2029-02-28
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include "nosqlprotocol.hh"
#include <bsoncxx/types/bson_value/value.hpp>

namespace nosql
{

/**
 * @class Extraction
 *
 * Captures whether a particular field should be included, excluded or replaced.
 */
class Extraction
{
public:
    enum class Action
    {
        INCLUDE,
        EXCLUDE,
        REPLACE
    };

    Extraction() = default;

    Extraction(const Extraction&) = default;
    Extraction(Extraction&&) = default;

    Extraction& operator = (const Extraction&) = default;

    Extraction(std::string_view name, Action action)
        : m_name(name)
        , m_action(action)
    {
        mxb_assert(m_action != Action::REPLACE);
    }

    Extraction(std::string_view name, bsoncxx::types::bson_value::view value);

    bool is_ok() const
    {
        return !m_name.empty();
    }

    bool is_exclude() const
    {
        mxb_assert(is_ok());
        return m_action == Action::EXCLUDE;
    }

    bool is_include() const
    {
        mxb_assert(is_ok());
        return m_action == Action::INCLUDE;
    }

    bool is_replace() const
    {
        mxb_assert(is_ok());
        return m_action == Action::REPLACE;
    }

    const std::string& name() const
    {
        return m_name;
    }

    Action action() const
    {
        return m_action;
    }

    bsoncxx::types::bson_value::value value(const bsoncxx::document::view& doc) const
    {
        mxb_assert(m_action == Action::REPLACE);
        mxb_assert(m_sReplacement);

        return m_sReplacement->value(doc);
    }

private:
    class Replacement
    {
    public:
        virtual ~Replacement() {}

        virtual bsoncxx::types::bson_value::value value(const bsoncxx::document::view& doc) const = 0;
    };

    class ValueReplacement;
    class VariableReplacement;
    class OperatorReplacement;

    static std::shared_ptr<Replacement> create_replacement(bsoncxx::types::bson_value::view value);

    std::string                  m_name;
    Action                       m_action { Action::REPLACE };
    std::shared_ptr<Replacement> m_sReplacement;
};

/**
 * @class Extractions
 *
 * Collection of Extraction(s).
 */
class Extractions
{
public:
    enum class Projection
    {
        COMPLETE,
        INCOMPLETE,
    };

    using const_iterator = std::vector<Extraction>::const_iterator;
    using iterator = std::vector<Extraction>::iterator;

    Extractions() = default;

    Extractions& operator = (const Extractions&) = default;

    static Extractions from_projection(const bsoncxx::document::view& projection);

    std::pair<std::string, Projection> generate_column() const;
    std::pair<std::string, Projection> generate_column(const std::string& doc) const;

    void swap(Extractions& rhs)
    {
        std::swap(m_extractions, rhs.m_extractions);
        std::swap(m_nInclusions, rhs.m_nInclusions);
        std::swap(m_nExclusions, rhs.m_nExclusions);
    }

    enum class Kind
    {
        INCLUDING,
        EXCLUDING
    };

    Kind kind() const
    {
        return m_nExclusions ? Kind::EXCLUDING : Kind::INCLUDING;
    }

    bool is_including() const
    {
        return kind() == Kind::INCLUDING;
    }

    bool is_excluding() const
    {
        return kind() == Kind::EXCLUDING;
    }

    bool empty() const
    {
        return m_extractions.empty();
    }

    const_iterator begin() const
    {
        return m_extractions.begin();
    }

    iterator begin()
    {
        return m_extractions.begin();
    }

    const_iterator end() const
    {
        return m_extractions.end();
    }

    iterator end()
    {
        return m_extractions.end();
    }

    void push_back(Extraction&& e)
    {
        if (e.name() != "_id")
        {
            if (e.is_exclude())
            {
                mxb_assert(m_nInclusions == 0);
                ++m_nExclusions;
            }
            else
            {
                mxb_assert(m_nExclusions == 0);
                ++m_nInclusions;
            }
        }

        m_extractions.emplace_back(std::move(e));
    }

    void push_back(const Extraction& e)
    {
        Extraction copy(e);

        push_back(Extraction(e));
    }

    void include_id()
    {
        // Add _id to the front, so that it will be first.
        m_extractions.insert(m_extractions.begin(), Extraction { "_id", Extraction::Action::INCLUDE });
    }

private:
    std::vector<Extraction> m_extractions;
    int32_t                 m_nInclusions { 0 };
    int32_t                 m_nExclusions { 0 };
};

}
