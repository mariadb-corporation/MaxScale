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

using namespace std;

namespace nosql
{

namespace
{

std::string project_process_excludes(string& doc, Extractions& extractions)
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

std::string nosql::Extractions::generate_column() const
{
    return generate_column("doc");
}

std::string nosql::Extractions::generate_column(const string& original_doc) const
{
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
                {
                    string replacement;

                    auto value = extraction.value();

                    switch (value.type())
                    {
                    case bsoncxx::type::k_utf8:
                        {
                            string_view s = value.get_utf8();

                            if (s == "$$ROOT")
                            {
                                replacement = original_doc;
                            }
                            else if (s.substr(0, 2) == "$$")
                            {
                                // TODO: Fix this.
                                SoftError("Only '$$ROOT' can be used as object in a projection",
                                          error::INTERNAL_ERROR);
                            }
                            else
                            {
                                replacement = nobson::to_json_expression(value);
                            }
                        }
                        break;

                    case bsoncxx::type::k_document:
                        {
                            bsoncxx::document::view sub_projection = value.get_document();
                            if (sub_projection.empty())
                            {
                                throw SoftError("An empty sub-projection is not a valid value. "
                                                "Found empty object at path", error::LOCATION51270);
                            }

                            auto sub_element = *sub_projection.begin();

                            if (sub_element.key() == "$bsonSize")
                            {
                                if (sub_element.type() == bsoncxx::type::k_utf8)
                                {
                                    string_view s = sub_element.get_string();
                                    if (s == "$$ROOT")
                                    {
                                        // TODO: The length of a JSON document is not the same as
                                        // TODO: the length of the equivalent BSON document.
                                        replacement = "LENGTH(" + original_doc + ")";
                                    }
                                    else
                                    {
                                        throw SoftError("$bsonSize requires a document input, found: string",
                                                        error::INTERNAL_ERROR);
                                    }
                                }
                                else
                                {
                                    throw SoftError("Only the value \"$$ROOT\" can be used as value for "
                                                    "$bsonSize", error::INTERNAL_ERROR);
                                }
                            }
                            else
                            {
                                throw SoftError("Only $bsonSize is allowed as operator in a projection",
                                                error::INTERNAL_ERROR);
                            }
                        }
                        break;

                    default:
                        replacement = nobson::to_json_expression(value);
                    }

                    ss << build_json_object(name, replacement, Extraction::Action::REPLACE);
                }
            }
        }

        ss << ")";

        start = ss.str();
    }

    return start;
}

}
