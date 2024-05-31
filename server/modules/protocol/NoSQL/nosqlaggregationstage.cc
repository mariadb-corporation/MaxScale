/*
 * Copyright (c) 2024 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-05-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "nosqlaggregationstage.hh"
#include <sstream>
#include "nosqlaggregationoperator.hh"
#include <map>

using namespace std;

namespace nosql
{

namespace aggregation
{

namespace
{

Stage::OperatorCreator unsupported_operator = [](bsoncxx::document::element op) {
    stringstream ss;
    ss << "Unsupported operator '" << op.key() << "'";

    throw SoftError(ss.str(), error::INTERNAL_ERROR);

    return unique_ptr<Operator>();
};

using StageCreator = unique_ptr<Stage>(*)(bsoncxx::document::element);
using Stages = map<string_view, StageCreator, less<>>;

Stages stages =
{
    { Group::NAME, &Group::create }
};

}

/**
 * Stage
 */

Stage::~Stage()
{
}

//static
unique_ptr<Stage> Stage::get(bsoncxx::document::element element)
{
    auto it = stages.find(element.key());

    if (it == stages.end())
    {
        stringstream ss;
        ss << "Unrecognized pipeline stage name: '" << element.key() << "'";

        throw SoftError(ss.str(), error::LOCATION40324);
    }

    return it->second(element);
}


/**
 * Group
 */
Stage::Operators Group::s_available_operators =
{
    { "$addToSet", unsupported_operator },
    { "$avg", unsupported_operator },
    {
        "$first", [](bsoncxx::document::element element) {
            return std::make_unique<First>(element);
        }
    },
    { "$last", unsupported_operator },
    { "$max", unsupported_operator },
    { "$mergeObjects", unsupported_operator },
    { "$min", unsupported_operator },
    { "$push", unsupported_operator },
    { "$stdDevPop", unsupported_operator },
    { "$sum", unsupported_operator },
};

unique_ptr<Stage> Group::create(bsoncxx::document::element element)
{
    mxb_assert(NAME == element.key());

    if (element.type() != bsoncxx::type::k_document)
    {
        throw SoftError("a group's fields must be specified in an object", error::LOCATION15947);
    }

    return unique_ptr<Group>(new Group(element.get_document()));
}

Group::Group(bsoncxx::document::view group)
{
    bool id_present = false;

    for (auto it = group.begin(); it != group.end(); ++it)
    {
        auto def = *it;

        if (def.key() == "_id")
        {
            if (def.type() != bsoncxx::type::k_null)
            {
                throw SoftError("Can only handle _id = null.", error::INTERNAL_ERROR);
            }

            id_present = true;
            continue;
        }

        add_operator(def);
    }

    if (!id_present)
    {
        throw SoftError("a group specification must include an _id", error::LOCATION15955);
    }
}

vector<mxb::Json> Group::process(vector<mxb::Json>& docs)
{
    for (mxb::Json doc : docs)
    {
        for (const NamedOperator& nop : m_operators)
        {
            nop.sOperator->process(doc);
        }
    }

    mxb::Json doc;
    doc.set_null("_id");

    for (const NamedOperator& nop : m_operators)
    {
        nop.sOperator->update(doc, string(nop.name));
    }

    vector<mxb::Json> rv;

    rv.emplace_back(std::move(doc));

    return rv;
}

void Group::add_operator(bsoncxx::document::element operator_def)
{
    string_view name = operator_def.key();

    if (operator_def.type() != bsoncxx::type::k_document)
    {
        stringstream ss;
        ss << "The field '" << name << "' must be an accumulator object";

        SoftError(ss.str(), error::LOCATION40234);
    }

    add_operator(name, operator_def.get_document());
}

void Group::add_operator(std::string_view name, bsoncxx::document::view def)
{
    auto it = def.begin();

    if (it == def.end())
    {
        stringstream ss;
        ss << "The field '" << name << "' must specify one accumulator";

        throw SoftError(ss.str(), error::LOCATION40238);
    }

    auto element = *it;

    auto jt = s_available_operators.find(element.key());

    if (jt == s_available_operators.end())
    {
        stringstream ss;
        ss << "Unknown group operator '" << element.key() << "'";

        throw SoftError(ss.str(), error::LOCATION15952);
    }

    auto sOperator = jt->second(element);

    m_operators.emplace_back(NamedOperator { name, std::move(sOperator) });
}

}
}

