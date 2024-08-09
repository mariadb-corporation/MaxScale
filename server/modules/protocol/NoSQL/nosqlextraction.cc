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

#include "nosqlextraction.hh"
#include <sstream>
#include "nosqlbase.hh"
#include "nosqlnobson.hh"
#include "nosqlaggregationoperator.hh"

using namespace std;

namespace nosql
{

namespace
{

string project_process_excludes(string& doc, Extractions& extractions)
{
    stringstream ss;
    int nExcludes = 0;

    bool is_exclusion = false;
    Extractions non_excludes;
    for (const auto& extraction : extractions)
    {
        if (extraction.is_exclude())
        {
            if (extraction.name() != "_id")
            {
                is_exclusion = true;
            }

            if (nExcludes++ == 0)
            {
                ss << "JSON_REMOVE(" << doc;
            }

            ss << ", '$." << extraction.name() << "'";
        }
        else
        {
            non_excludes.push_back(extraction);
        }
    }

    if (nExcludes != 0)
    {
        ss << ")";

        doc = ss.str();
    }

    extractions.swap(non_excludes);

    return extractions.empty() || is_exclusion ? doc : "JSON_OBJECT()";
}

void build_json_object(ostream& out,
                       const string& path,
                       const string& original_path,
                       const string& doc,
                       Extraction::Action action)
{
    mxb_assert(action != Extraction::Action::EXCLUDE);

    out << "JSON_OBJECT(";

    auto pos = path.find('.');
    string head = path.substr(0, pos);
    string tail = (pos != string::npos ? path.substr(pos + 1) : string());

    if (!tail.empty())
    {
        out << "'" << head << "', ";
        build_json_object(out, tail, original_path, doc, action);
    }
    else
    {
        if (action == Extraction::Action::INCLUDE)
        {
            out << "'" << head << "', JSON_EXTRACT(" << doc << ", '$." << original_path << "')";
        }
        else
        {
            out << "'" << head << "', " << doc;
        }
    }

    out << ")";
}

string build_json_object(const string& path, const string& doc, Extraction::Action action)
{
    mxb_assert(action != Extraction::Action::EXCLUDE);

    stringstream ss;

    build_json_object(ss, path, path, doc, action);

    return ss.str();
}

}

class Extraction::ValueReplacement : public Extraction::Replacement
{
public:
    ValueReplacement(bsoncxx::types::bson_value::view value)
        : m_value(value)
    {
    }

    bsoncxx::types::bson_value::view value(const bsoncxx::document::view& doc) const override
    {
        return m_value;
    }

private:
    bsoncxx::types::bson_value::view m_value;
};

namespace
{

bsoncxx::types::bson_value::view to_bson_value_view(const bsoncxx::document::view& doc)
{
    bsoncxx::types::b_document b_doc {doc};
    bsoncxx::types::bson_value::view b_view {b_doc};

    return b_view;
}

}

class Extraction::VariableReplacement : public Extraction::Replacement
{
public:
    VariableReplacement(std::string_view variable)
        : m_variable(variable)
    {
        if (m_variable != "$$ROOT")
        {
            throw SoftError("Currently only $$ROOT is supported.", error::INTERNAL_ERROR);
        }
    }

    bsoncxx::types::bson_value::view value(const bsoncxx::document::view& doc) const override
    {
        mxb_assert(m_variable == "$$ROOT");

        return to_bson_value_view(doc);
    }

private:
    std::string m_variable;
};

class Extraction::OperatorReplacement : public Extraction::Replacement
{
public:
    OperatorReplacement(const bsoncxx::document::view& doc)
        : m_sOperator(aggregation::Operator::create(to_bson_value_view(doc)))
    {
    }

    bsoncxx::types::bson_value::view value(const bsoncxx::document::view& doc) const override
    {
        return m_sOperator->process(doc);
    }

private:
    std::unique_ptr<aggregation::Operator> m_sOperator;
};



Extraction::Extraction(std::string_view name, bsoncxx::types::bson_value::view value)
    : m_name(name)
    , m_action(Action::REPLACE)
    , m_sReplacement(create_replacement(value))
{
}

//static
std::shared_ptr<Extraction::Replacement>
Extraction::create_replacement(bsoncxx::types::bson_value::view value)
{
    std::shared_ptr<Extraction::Replacement> sReplacement;

    switch (value.type())
    {
    case bsoncxx::type::k_string:
        {
            string_view s = value.get_string();

            if (s.find("$$") == 0)
            {
                sReplacement = std::make_shared<VariableReplacement>(s);
            }
        }
        break;

    case bsoncxx::type::k_document:
        sReplacement = std::make_shared<OperatorReplacement>(value.get_document());
        break;

    default:
        break;
    }

    if (!sReplacement)
    {
        sReplacement = std::make_shared<ValueReplacement>(value);
    }

    return sReplacement;
}

// static
Extractions Extractions::from_projection(const bsoncxx::document::view& projection)
{
    Extractions extractions;

    auto it = projection.begin();
    auto end = projection.end();

    if (it != end)
    {
        bool inclusions = false;
        bool exclusions = false;
        bool id_seen = false;

        for (; it != end; ++it)
        {
            bsoncxx::document::element element = *it;
            const auto& key = element.key();

            if (key.size() == 0)
            {
                continue;
            }

            auto value = element.get_value();

            Extraction::Action action;

            if (key == "_id")
            {
                id_seen = true;
            }

            switch (element.type())
            {
                case bsoncxx::type::k_bool:
                case bsoncxx::type::k_decimal128:
                case bsoncxx::type::k_double:
                case bsoncxx::type::k_int32:
                case bsoncxx::type::k_int64:
                    if (nobson::is_truthy(value))
                    {
                        if (exclusions && key != "_id")
                        {
                            stringstream ss;
                            ss << "Invalid $project :: caused by :: Cannot do inclusion on field "
                               << key << " in exclusion projection";

                            throw SoftError(ss.str(), error::LOCATION31253);
                        }

                        action = Extraction::Action::INCLUDE;

                        // '_id' can always be included, so it is ignored.
                        if (key != "_id")
                        {
                            inclusions = true;
                        }
                    }
                    else
                    {
                        if (inclusions && key != "_id")
                        {
                            stringstream ss;
                            ss << "Invalid $project :: caused by :: Cannot do exclusion on field "
                               << key << " in inclusion projection";

                            throw SoftError(ss.str(), error::LOCATION31253);
                        }

                        action = Extraction::Action::EXCLUDE;

                        // '_id' can always be excluded, so it is ignored.
                        if (key != "_id")
                        {
                            exclusions = true;
                        }
                    }
                    break;

                default:
                    if (exclusions && key != "_id")
                    {
                        stringstream ss;
                        ss << "Invalid $project :: caused by :: Cannot use an expression "
                           << key << " ... in an exclusion projection"; // TODO: Convert value to string

                        throw SoftError(ss.str(), error::LOCATION31310);
                    }

                    action = Extraction::Action::REPLACE;

                    // '_id' can always be replaced, so it is ignored.
                    if (key != "_id")
                    {
                        inclusions = true;
                    }
                    break;
            }

            auto name = escape_essential_chars(static_cast<string>(key));

            if (action != Extraction::Action::REPLACE)
            {
                extractions.push_back(Extraction { name, action });
            }
            else
            {
                extractions.push_back(Extraction { name, value });
            }
        }

        if (!id_seen)
        {
            extractions.include_id();
        }
    }

    return extractions;
}

pair<string, Extractions::Projection> Extractions::generate_column() const
{
    return generate_column("doc");
}

pair<string, Extractions::Projection> Extractions::generate_column(const string& original_doc) const
{
    Projection projection = Projection::COMPLETE;

    string doc = original_doc;
    Extractions extractions = *this;

    string start = project_process_excludes(doc, extractions);

    if (!extractions.empty())
    {
        stringstream ss;
        ss << "JSON_MERGE_PATCH(" << start;

        for (const auto& extraction : extractions)
        {
            ss << ", ";
            auto& name = extraction.name(); // TODO: 'name' needs "." handling.

            auto action = extraction.action();

            switch (action)
            {
            case Extraction::Action::INCLUDE:
                ss << "CASE WHEN JSON_EXISTS(" << doc << ", '$."  << name << "') "
                   << "THEN " << build_json_object(name, doc, action)
                   << "ELSE JSON_OBJECT() "
                   << "END";
                break;

            case Extraction::Action::EXCLUDE:
                // None should be left.
                mxb_assert(!true);
                break;

            case Extraction::Action::REPLACE:
                projection = Projection::INCOMPLETE;
                break;
            }
        }

        if (projection == Projection::INCOMPLETE)
        {
            // All actions are handled in the post-procssing phase.
            start = original_doc;
        }
        else
        {
            ss << ")";
            start = ss.str();
        }
    }

    return make_pair(start, projection);
}

}
