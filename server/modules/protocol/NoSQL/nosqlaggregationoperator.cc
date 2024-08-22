/*
 * Copyright (c) 2024 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-09-09
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "nosqlaggregationoperator.hh"
#include <time.h>
#include <iomanip>
#include <map>
#include <optional>
#include <sstream>
#include "nosqlnobson.hh"
#include <bsoncxx/types/bson_value/value.hpp>

using namespace std;
namespace json = mxb::json;

namespace
{

// bsoncxx::to_string(bsoncxx::type) does not return the same names as the
// ones used by the test-programs. Hence this mapping is needed.

map<string, bsoncxx::type, less<>> type_codes_by_name =
{
    { "array", bsoncxx::type::k_array },
    { "binData", bsoncxx::type::k_binary },
    { "bool", bsoncxx::type::k_bool },
    { "date", bsoncxx::type::k_date },
    { "dbPointer", bsoncxx::type::k_dbpointer },
    { "decimal", bsoncxx::type::k_decimal128 },
    { "double", bsoncxx::type::k_double },
    { "int", bsoncxx::type::k_int32 },
    { "javascript", bsoncxx::type::k_code },
    { "javascriptWithScope", bsoncxx::type::k_codewscope },
    { "long", bsoncxx::type::k_int64 },
    { "minKey", bsoncxx::type::k_minkey },
    { "object", bsoncxx::type::k_document },
    { "objectId", bsoncxx::type::k_oid },
    { "regex", bsoncxx::type::k_regex },
    { "string", bsoncxx::type::k_utf8 },
    { "symbol", bsoncxx::type::k_symbol },
    { "timestamp", bsoncxx::type::k_timestamp },
    { "undefined", bsoncxx::type::k_undefined },
};

map<bsoncxx::type, string, less<>> type_names_by_code = [](const map<string, bsoncxx::type, less<>>& in) {
    map<bsoncxx::type, string, less<>> out;

    for (const auto& kv : in)
    {
        out.emplace(make_pair(kv.second, kv.first));
    }

    return out;
}(type_codes_by_name);

}

namespace nosql
{

namespace aggregation
{

namespace
{

struct CreatorEntry
{
    Operator::Creator        create;
    const Operator::TypeSet& allowed_literals;
};

#define NOSQL_OPERATOR(O) { O::NAME, CreatorEntry { O::create, O::ALLOWED_LITERALS } }

map<string, CreatorEntry, less<>> operators =
{
    NOSQL_OPERATOR(Abs),
    NOSQL_OPERATOR(Add),
    NOSQL_OPERATOR(And),
    NOSQL_OPERATOR(ArrayElemAt),
    NOSQL_OPERATOR(BsonSize),
    NOSQL_OPERATOR(Ceil),
    NOSQL_OPERATOR(Cmp),
    NOSQL_OPERATOR(Concat),
    NOSQL_OPERATOR(Cond),
    NOSQL_OPERATOR(Convert),
    NOSQL_OPERATOR(Divide),
    NOSQL_OPERATOR(Eq),
    NOSQL_OPERATOR(Exp),
    NOSQL_OPERATOR(First),
    NOSQL_OPERATOR(Floor),
    NOSQL_OPERATOR(Gt),
    NOSQL_OPERATOR(Gte),
    NOSQL_OPERATOR(IfNull),
    NOSQL_OPERATOR(IsArray),
    NOSQL_OPERATOR(IsNumber),
    NOSQL_OPERATOR(Last),
    NOSQL_OPERATOR(Literal),
    NOSQL_OPERATOR(Ln),
    NOSQL_OPERATOR(Log),
    NOSQL_OPERATOR(Log10),
    NOSQL_OPERATOR(Lt),
    NOSQL_OPERATOR(Lte),
    NOSQL_OPERATOR(Mod),
    NOSQL_OPERATOR(Multiply),
    NOSQL_OPERATOR(Ne),
    NOSQL_OPERATOR(Not),
    NOSQL_OPERATOR(Or),
    NOSQL_OPERATOR(Pow),
    NOSQL_OPERATOR(Sqrt),
    NOSQL_OPERATOR(Size),
    NOSQL_OPERATOR(Subtract),
    NOSQL_OPERATOR(Switch),
    NOSQL_OPERATOR(ToBool),
    NOSQL_OPERATOR(ToDate),
    NOSQL_OPERATOR(ToDecimal),
    NOSQL_OPERATOR(ToDouble),
    NOSQL_OPERATOR(ToInt),
    NOSQL_OPERATOR(ToLong),
    NOSQL_OPERATOR(ToObjectId),
    NOSQL_OPERATOR(ToString),
    NOSQL_OPERATOR(Type),
};

}


/**
 * Operator
 */
Operator::~Operator()
{
}

//static
unique_ptr<Operator> Operator::create(const BsonView& value, const TypeSet& literal_types)
{
    unique_ptr<Operator> sOp;

    switch (value.type())
    {
    case bsoncxx::type::k_utf8:
        {
            string_view s = value.get_utf8();

            if (!s.empty() && s.front() == '$')
            {
                sOp = Accessor::create(value);
            }
            else
            {
                sOp = Literal::create(value);
            }
        }
        break;

    case bsoncxx::type::k_document:
        {
            bsoncxx::document::view doc = value.get_document();

            auto it = doc.begin();

            if (it == doc.end())
            {
                sOp = Literal::create(value);
            }
            else
            {
                bsoncxx::document::element op = *it;

                string_view s = op.key();

                if (!s.empty() && s.front() == '$')
                {
                    auto jt = operators.find(s);

                    if (jt != operators.end())
                    {
                        sOp = jt->second.create(op.get_value());
                    }
                    else
                    {
                        stringstream ss;
                        ss << "Unrecognized expression '" << op.key() << "'";

                        throw SoftError(ss.str(), error::INVALID_PIPELINE_OPERATOR);
                    }
                }
                else
                {
                    sOp = MultiAccessor::create(value);
                }
            }
        }
        break;

    case bsoncxx::type::k_array:
        {
            bsoncxx::array::view array = value.get_array();
            auto it = array.begin();
            auto end = array.end();
            int32_t n = 0;
            // No array.length(), must iterate.
            while (it != end)
            {
                ++n;
                ++it;
            }

            if (n != 1)
            {
                stringstream ss;
                ss << "Exactly 1 argument expected. " << n << " were passed in.";
                throw SoftError(ss.str(), error::LOCATION16020);
            }

            it = array.begin();
            auto element = *it;

            // If the single element is an array, it is treated as a literal. Otherwise
            // we behave is if the element had been provided without the enclosing array.
            if (element.type() == bsoncxx::type::k_array)
            {
                sOp = Literal::create(element.get_value());
            }
            else
            {
                sOp = create(element.get_value(), literal_types);
            }
        }
        break;

    default:
        sOp = Literal::create(value);
    }

    return sOp;
}


void Operator::append(DocumentBuilder& builder, std::string_view key, bsoncxx::document::view doc)
{
    append(builder, key, process(doc));
}

//static
void Operator::append(DocumentBuilder& builder, std::string_view key, const BsonValue& value)
{
    auto view = value.view();

    if (view.type() != bsoncxx::type::k_string
        || static_cast<string_view>(view.get_string()) != "$$REMOVE")
    {
        builder.append(kvp(key, value));
    }
}

namespace
{

void throw_count_error(const char* zOp, size_t nMin, size_t nMax, size_t n)
{
    stringstream ss;
    ss << "Expression " << zOp << " takes ";

    if (nMin == nMax)
    {
        ss << "exactly " << nMin << " arguments. ";
    }
    else if (nMax == std::numeric_limits<size_t>::max())
    {
        ss << "at least " << nMin << " arguments. ";
    }
    else
    {
        ss << "between " << nMin << " and " << nMax << " arguments. ";
    }

    ss << n << " was provided.";

    throw SoftError(ss.str(), error::LOCATION16020);
}

}

vector<unique_ptr<Operator>> Operator::create_operators(const BsonView& value,
                                                        const char* zOp,
                                                        size_t nMin,
                                                        size_t nMax,
                                                        const set<bsoncxx::type>& types)
{
    vector<unique_ptr<Operator>> rv;

    if (value.type() == bsoncxx::type::k_array)
    {
        rv = create_operators(value.get_array(), zOp, nMin, nMax, types);
    }
    else
    {
        if (nMin > 1)
        {
            throw_count_error(zOp, nMin, nMax, 1);
        }

        rv.emplace_back(create_operator(value, zOp, types));
    }

    return rv;
}

vector<unique_ptr<Operator>> Operator::create_operators(const bsoncxx::array::view& array,
                                                        const char* zOp,
                                                        size_t nMin,
                                                        size_t nMax,
                                                        const set<bsoncxx::type>& types)
{
    vector<unique_ptr<Operator>> rv;

    size_t n = 0;

    for (const auto& element : array)
    {
        rv.emplace_back(create_operator(element.get_value(), zOp, types));
        ++n;
    }

    if (n < nMin || n > nMax)
    {
        throw_count_error(zOp, nMin, nMax, n);
    }

    return rv;
}

unique_ptr<Operator> Operator::create_operator(const BsonView& value,
                                               const char* zOp,
                                               const set<bsoncxx::type>& literal_types)
{
    unique_ptr<Operator> sOp;

    bool indirect = false;
    switch (value.type())
    {
    case bsoncxx::type::k_utf8:
        {
            string_view s = value.get_utf8();
            if (!s.empty() && s.front() == '$')
            {
                indirect = true;
            }
        }
        break;

    case bsoncxx::type::k_document:
        indirect = true;
        break;

    default:
        ;
    }

    if (!indirect)
    {
        if (!literal_types.empty() && literal_types.count(value.type()) == 0)
        {
            stringstream ss;
            ss << zOp << " only supports types ";
            for (auto type : literal_types)
            {
                ss << bsoncxx::to_string(type) << ", ";
            }

            ss << "not " << bsoncxx::to_string(value.type());

            throw SoftError(ss.str(), error::TYPE_MISMATCH);
        }
    }

    if (indirect)
    {
        sOp = std::move(Operator::create(value, literal_types));
    }
    else
    {
        sOp = std::move(Literal::create(value));
    }

    return sOp;
}

/**
 * Operator::Accessor
 */
Operator::Accessor::Accessor(const BsonView& value)
{
    mxb_assert(value.type() == bsoncxx::type::k_utf8);

    string_view field = value.get_utf8();

    mxb_assert(!field.empty() && field.front() == '$');

    size_t from = 1; // Skip the '$'
    auto to = field.find_first_of('.');

    if (to != string_view::npos)
    {
        do
        {
            m_fields.emplace_back(string(field.substr(from, to - from)));
            from = to + 1;
            to = field.find_first_of('.', from);
        }
        while (to != string_view::npos);
    }

    m_fields.emplace_back(string(field.substr(from)));
}

bsoncxx::types::bson_value::value Operator::Accessor::process(bsoncxx::document::view doc)
{
    bool found;
    return process(doc, &found);
}

bsoncxx::types::bson_value::value Operator::Accessor::process(bsoncxx::document::view doc, bool* pFound)
{
    BsonValue rv(nullptr);

    *pFound = false;

    bsoncxx::document::element element;

    auto it = m_fields.begin();
    do
    {
        element = doc[*it];

        if (!element)
        {
            break;
        }

        ++it;

        if (it == m_fields.end())
        {
            *pFound = true;
            rv = BsonValue(element.get_value());
        }
        else
        {
            if (element.type() == bsoncxx::type::k_document)
            {
                doc = element.get_document();
            }
            else
            {
                doc = bsoncxx::document::view();
            }
        }
    }
    while (!doc.empty() && it != m_fields.end());

    return rv;
}

void Operator::Accessor::append(DocumentBuilder& builder,
                                std::string_view key,
                                bsoncxx::document::view doc)
{
    bool found;
    BsonValue value = process(doc, &found);

    if (found)
    {
        Operator::append(builder, key, value);
    }
}

/**
 * Operator::MultiAccessor
 */
Operator::MultiAccessor::MultiAccessor(const BsonView& value)
{
    mxb_assert(value.type() == bsoncxx::type::k_document);

    bsoncxx::document::view doc = value.get_document();

    for (const auto& element : doc)
    {
        m_fields.emplace_back(Field { string(element.key()), Operator::create(element.get_value()) });
    }
}

bsoncxx::types::bson_value::value Operator::MultiAccessor::process(bsoncxx::document::view doc)
{
    DocumentBuilder builder;

    for (const auto& field : m_fields)
    {
        builder.append(kvp(field.name, field.sOp->process(doc)));
    }

    return builder.extract().view();
}

/**
 * Abs
 */
bsoncxx::types::bson_value::value Abs::process(bsoncxx::document::view doc)
{
    auto rv = m_sOp->process(doc);

    if (!nobson::is_null(rv))
    {
        if (!nobson::is_number(rv, nobson::NumberApproach::REJECT_DECIMAL128))
        {
            stringstream ss;
            ss << "$abs only supports numeric, types, not " << bsoncxx::to_string(rv.view().type());

            throw SoftError(ss.str(), error::LOCATION28765);
        }

        rv = nobson::abs(rv);
    }

    return rv;
}

/**
 * Add
 */
bsoncxx::types::bson_value::value Add::process(bsoncxx::document::view doc)
{
    BsonValue rv(nullptr);

    for (auto& sOp : m_ops)
    {
        BsonValue value = sOp->process(doc);

        if (nobson::is_number(value, nobson::NumberApproach::REJECT_DECIMAL128))
        {
            if (nobson::is_null(rv))
            {
                rv = value;
            }
            else
            {
                rv = nobson::add(rv, value);
            }
        }
    }

    return rv;
}

/**
 * And
 */
bsoncxx::types::bson_value::value And::process(bsoncxx::document::view doc)
{
    bool rv = true;

    for (const auto& sOp : m_ops)
    {
        rv = nobson::is_truthy(sOp->process(doc));

        if (!rv)
        {
            break;
        }
    }

    return bsoncxx::types::bson_value::value(rv);
}

/**
 * ArrayElemAt
 */
bsoncxx::types::bson_value::value ArrayElemAt::process(bsoncxx::document::view doc)
{
    bool null_is_ok;
    return process(doc, &null_is_ok);
}

bsoncxx::types::bson_value::value ArrayElemAt::process(bsoncxx::document::view doc, bool* pNull_is_ok)
{
    *pNull_is_ok = false;

    BsonValue avalue = m_ops[0]->process(doc);
    BsonView aview = avalue.view();
    auto type = aview.type();

    if (type == bsoncxx::type::k_null || type == bsoncxx::type::k_undefined)
    {
        *pNull_is_ok = true;
        return BsonValue(nullptr);
    }

    if (type != bsoncxx::type::k_array)
    {
        stringstream serr;
        serr << "$arrayElemAt's first argument must be array, but is " << bsoncxx::to_string(type);

        throw SoftError(serr.str(), error::LOCATION28689);
    }

    BsonValue ivalue = m_ops[1]->process(doc);
    BsonView iview = ivalue.view();
    type = iview.type();

    int32_t index = 0;

    switch (type)
    {
    case bsoncxx::type::k_null:
        *pNull_is_ok = true;
        return BsonValue(nullptr);

    case bsoncxx::type::k_int32:
        index = iview.get_int32();
        break;

    case bsoncxx::type::k_int64:
        index = iview.get_int64();
        break;

    case bsoncxx::type::k_double:
        {
            double d = iview.get_double();
            index = d;

            if (index != d)
            {
                stringstream serr;
                serr << "$arrayElemAt's second argument must be representable as a 32-bit integer: "
                     << d;

                throw SoftError(serr.str(), error::LOCATION28691);
            }
        }
        break;

    case bsoncxx::type::k_decimal128:
        {
            auto d128 = iview.get_decimal128();
            auto s = d128.value.to_string();
            auto d = std::stod(s);

            index = d;

            if (index != d)
            {
                stringstream serr;
                serr << "$arrayElemAt's second argument must be representable as a 32-bit integer: "
                     << d;

                throw SoftError(serr.str(), error::LOCATION28691);
            }
        }
        break;

    default:
        {
            stringstream serr;
            serr << "$arrayElemAt's second argument must be a numeric value, but is "
                 << bsoncxx::to_string(type);

            throw SoftError(serr.str(), error::LOCATION28690);
        }
    }

    return access(aview.get_array(), index);
}

void ArrayElemAt::append(DocumentBuilder& builder,
                         std::string_view key,
                         bsoncxx::document::view doc)
{
    bool null_is_ok;

    auto value = process(doc, &null_is_ok);

    if (value.view().type() != bsoncxx::type::k_null || null_is_ok)
    {
        Base::append(builder, key, value);
    }
}

//static
bsoncxx::types::bson_value::value ArrayElemAt::access(const bsoncxx::array::view& array, int32_t index)
{
    bsoncxx::array::view::iterator it;
    auto end = array.end();

    if (index >= 0)
    {
        it = array.find(index);
    }
    else
    {
        auto begin = array.begin();
        auto size = std::distance(array.begin(), end);

        index = size + index;

        if (index < 0)
        {
            it = end;
        }
        else
        {
            it = array.find(index);
        }
    }

    return it == end ? BsonValue(nullptr) : BsonValue(it->get_value());
}

/**
 * BsonSize
 */
bsoncxx::types::bson_value::value BsonSize::process(bsoncxx::document::view doc)
{
    int64_t size = doc.length();

    return bsoncxx::types::bson_value::value(size);
}

/**
 * Ceil
 */
bsoncxx::types::bson_value::value Ceil::process(bsoncxx::document::view doc)
{
    auto value = m_sOp->process(doc);

    if (!nobson::is_null(value))
    {
        if (!nobson::is_number(value, nobson::NumberApproach::REJECT_DECIMAL128))
        {
            stringstream ss;
            ss << "$ceil only supports numeric types, not " << bsoncxx::to_string(value.view().type());

            throw SoftError(ss.str(), error::LOCATION28765);
        }

        value = nobson::ceil(value);
    }

    return value;
}

/**
 * Cmp
 */
bsoncxx::types::bson_value::value Cmp::process(bsoncxx::document::view doc)
{
    mxb_assert(m_ops.size() == 2);

    auto lhs = m_ops[0]->process(doc);
    auto rhs = m_ops[1]->process(doc);

    return BsonValue(nobson::compare(lhs, rhs));
}

/**
 * Concat
 */
bsoncxx::types::bson_value::value Concat::process(bsoncxx::document::view doc)
{
    string rv;

    for (const auto& sOp : m_ops)
    {
        auto value = sOp->process(doc).view();

        if (value.type() != bsoncxx::type::k_string)
        {
            stringstream ss;
            ss << "$concat supports only strings, not " << bsoncxx::to_string(value.type());
        }

        rv += value.get_string();
    }

    return BsonValue(rv);
}

/**
 * Cond
 */
Cond::Cond(const BsonView& value)
{
    int nArgs = 0;

    switch (value.type())
    {
    case bsoncxx::type::k_document:
        {
            m_ops.resize(3);

            struct
            {
                const char* zWhat;
                int error;
            } params[] =
            {
                {
                    "if", error::LOCATION17080
                },
                {
                    "then", error::LOCATION17081
                },
                {
                    "else", error::LOCATION17082
                }
            };

            bsoncxx::document::view doc = value.get_document();
            for (auto element : doc)
            {
                auto sOp = Operator::create(element.get_value());
                int index = -1;

                if (element.key() == "if")
                {
                    index = 0;
                }
                else if (element.key() == "then")
                {
                    index = 1;
                }
                else if (element.key() == "else")
                {
                    index = 2;
                }
                else
                {
                    stringstream serr;
                    serr << "Unrecognized parameter to $cond: " << element.key();

                    throw SoftError(serr.str(), error::LOCATION17083);
                }

                if (index != -1)
                {
                    m_ops[index] = std::move(sOp);
                    params[index].zWhat = nullptr;
                }
            }

            for (const auto param : params)
            {
                if (param.zWhat)
                {
                    stringstream serr;
                    serr << "Missing '" << param.zWhat << "' parameter to $cond";

                    throw SoftError(serr.str(), param.error);
                }
            }

            nArgs = 3;
        }
        break;

    case bsoncxx::type::k_array:
        {
            bsoncxx::array::view array = value.get_array();
            for (auto element : array)
            {
                m_ops.emplace_back(Operator::create(element.get_value()));
            }

            nArgs = m_ops.size();
        }
        break;

    default:
        nArgs = 1;
    }

    if (nArgs != 3)
    {
        stringstream ss;
        ss << "Expression $cond takes exactly 3 arguments. " << nArgs << " were passed in.";

        throw SoftError(ss.str(), error::LOCATION16020);
    }
}

bsoncxx::types::bson_value::value Cond::process(bsoncxx::document::view doc)
{
    mxb_assert(m_ops.size() == 3);

    BsonValue rv(nullptr);

    auto value = m_ops[0]->process(doc);
    BsonView cond { value };

    if (nobson::is_truthy(cond))
    {
        rv = m_ops[1]->process(doc);
    }
    else
    {
        rv = m_ops[2]->process(doc);
    }

    return rv;
}

/**
 * Convert
 */
Convert::Convert(const BsonView& value)
{
    if (value.type() != bsoncxx::type::k_document)
    {
        stringstream ss;
        ss << "$convert expects an object of named arguments but found: "
           << bsoncxx::to_string(value.type());

        throw SoftError(ss.str(), error::FAILED_TO_PARSE);
    }

    bsoncxx::document::view convert = value.get_document();

    bsoncxx::document::element input;
    bsoncxx::document::element to;

    for (auto a : convert)
    {
        string_view key = a.key();

        if (key == "input")
        {
            input = a;
        }
        else if (key == "to")
        {
            to = a;
        }
        else if (key == "onError")
        {
            m_on_error = a.get_value();
        }
        else if (key == "onNull")
        {
            m_on_null = a.get_value();
        }
        else
        {
            stringstream ss;
            ss << "$convert found an unknown argument: " << key;

            throw SoftError(ss.str(), error::FAILED_TO_PARSE);
        }
    }

    if (!input)
    {
        throw SoftError("Missing 'input' parameter to $convert", error::FAILED_TO_PARSE);
    }

    if (!to)
    {
        throw SoftError("Missing 'to' parameter to $convert", error::FAILED_TO_PARSE);
    }

    m_sInput = Operator::create(input.get_value());

    string_view s;
    if (to.type() == bsoncxx::type::k_string)
    {
        s = to.get_string();
    }

    if (!s.empty() && s.front() == '$')
    {
        m_to_field_path.reset(s);
    }
    else
    {
        m_to_convert = get_converter(to.get_value());
    }
}

bsoncxx::types::bson_value::value Convert::process(bsoncxx::document::view doc)
{
    BsonValue rv(nullptr);

    auto value = m_sInput->process(doc);

    if (!nobson::is_null(value) && !nobson::is_undefined(value))
    {
        Converter convert = nullptr;

        if (m_to_convert)
        {
            convert = m_to_convert;
        }
        else
        {
            convert = get_converter(m_to_field_path.get(doc).get_value());
        }

        rv = convert(value, m_on_error);
    }
    else if (!nobson::is_null(m_on_null))
    {
        rv = BsonValue(m_on_null);
    }

    return rv;
}

//static
bsoncxx::types::bson_value::value Convert::to_bool(const BsonView& value, const BsonView& on_error)
{
    switch (value.type())
    {
    case bsoncxx::type::k_array:
        return BsonValue(true);

    case bsoncxx::type::k_binary:
        return BsonValue(true);

    case bsoncxx::type::k_bool:
        return BsonValue(value.get_bool());

    case bsoncxx::type::k_code:
        return BsonValue(true);

    case bsoncxx::type::k_date:
        return BsonValue(true);

    case bsoncxx::type::k_decimal128:
        {
            bsoncxx::decimal128 d = value.get_decimal128().value;
            return BsonValue(d.high() != 0 || d.low() != 0 ? true : false);
        }

    case bsoncxx::type::k_double:
        return BsonValue(value.get_double() != 0);

    case bsoncxx::type::k_int32:
        return BsonValue(value.get_int32() != 0);

    case bsoncxx::type::k_codewscope:
        return BsonValue(true);

    case bsoncxx::type::k_int64:
        return BsonValue(value.get_int64() != 0);

    case bsoncxx::type::k_maxkey:
        return BsonValue(true);

    case bsoncxx::type::k_minkey:
        return BsonValue(true);

    case bsoncxx::type::k_null:
        // TODO: Deal with on_null.
        return BsonValue(nullptr);

    case bsoncxx::type::k_document:
        return BsonValue(true);

    case bsoncxx::type::k_oid:
        return BsonValue(true);

    case bsoncxx::type::k_regex:
        return BsonValue(true);

    case bsoncxx::type::k_utf8:
        return BsonValue(true);

    case bsoncxx::type::k_timestamp:
        return BsonValue(true);

    case bsoncxx::type::k_dbpointer:
        return BsonValue(true);

    case bsoncxx::type::k_undefined:
        return BsonValue(false);

    case bsoncxx::type::k_symbol:
        return BsonValue(true);
    }

    mxb_assert(!true);

    return BsonValue(nullptr);
}

//static
bsoncxx::types::bson_value::value Convert::to_date(const BsonView& value, const BsonView& on_error)
{
    switch (value.type())
    {
    case bsoncxx::type::k_date:
        return BsonValue(value.get_date());

    case bsoncxx::type::k_double:
        {
            std::chrono::milliseconds millis_since_epoch(value.get_double());
            return BsonValue(bsoncxx::types::b_date(millis_since_epoch));
        }

    case bsoncxx::type::k_decimal128:
        {
            bsoncxx::decimal128 d = value.get_decimal128().value;
            string s = d.to_string();
            std::chrono::milliseconds millis_since_epoch(std::stoll(s));
            return BsonValue(bsoncxx::types::b_date(millis_since_epoch));
        }

    case bsoncxx::type::k_int64:
        return BsonValue(bsoncxx::types::b_date(std::chrono::milliseconds(value.get_int64())));

    case bsoncxx::type::k_oid:
        {
            bsoncxx::oid oid = value.get_oid().value;
            auto tp = std::chrono::system_clock::from_time_t(oid.get_time_t());

            return BsonValue(bsoncxx::types::b_date {tp});
        }

    case bsoncxx::type::k_utf8:
        {
            string s = static_cast<string>(value.get_string());

            std::tm tm {};
            int ms = 0;

            int rv = sscanf(s.c_str(), "%d-%d-%dT%d:%d:%d.%dZ",
                            &tm.tm_year, &tm.tm_mon, &tm.tm_mday,
                            &tm.tm_hour, &tm.tm_min, &tm.tm_sec, &ms);

            if (rv == 0)
            {
                stringstream serr;
                serr << "Cannot convert the string \"" << static_cast<string_view>(value.get_string())
                     << "\" to an ISO date in $convert";
                throw SoftError(serr.str(), error::BAD_VALUE);
            }

            tm.tm_year -= 1900;
            tm.tm_mon -= 1;

            std::time_t t = timegm(&tm);

            std::chrono::system_clock::time_point tp = std::chrono::system_clock::from_time_t(t);

            tp += std::chrono::milliseconds(ms);


            return BsonValue(bsoncxx::types::b_date {tp});
        }

    default:
        break;
    }

    return handle_default_case(value.type(), bsoncxx::type::k_date, on_error);
}

//static
bsoncxx::types::bson_value::value Convert::to_decimal(const BsonView& value, const BsonView& on_error)
{
    switch (value.type())
    {
    case bsoncxx::type::k_bool:
        return BsonValue(bsoncxx::decimal128(value.get_bool() ? "1" : "0"));

    case bsoncxx::type::k_double:
        {
            ostringstream ss;
            ss << std::fixed << std::setprecision(14) << value.get_double(); // Trailing 0s are needed
            return BsonValue(bsoncxx::decimal128(ss.str()));
        }

    case bsoncxx::type::k_decimal128:
        return BsonValue(value.get_decimal128());

    case bsoncxx::type::k_int32:
        return BsonValue(bsoncxx::decimal128(std::to_string(value.get_int32())));

    case bsoncxx::type::k_int64:
        return BsonValue(bsoncxx::decimal128(std::to_string(value.get_int64())));

    case bsoncxx::type::k_utf8:
        return BsonValue(bsoncxx::decimal128(value.get_utf8()));

    case bsoncxx::type::k_date:
        return BsonValue(bsoncxx::decimal128(std::to_string(value.get_date().value.count())));

    default:
        break;
    }

    return handle_default_case(value.type(), bsoncxx::type::k_decimal128, on_error);
}

//static
bsoncxx::types::bson_value::value Convert::to_double(const BsonView& value, const BsonView& on_error)
{
    switch (value.type())
    {
    case bsoncxx::type::k_bool:
        return BsonValue((double)(value.get_bool() ? 1 : 0));;

    case bsoncxx::type::k_date:
        return BsonValue((double)value.get_date().to_int64());

    case bsoncxx::type::k_decimal128:
        {
            bsoncxx::decimal128 decimal128 = value.get_decimal128().value;
            double d;
            auto result = nobson::convert(decimal128, &d);

            if (result == nobson::ConversionResult::OK)
            {
                return BsonValue(d);
            }
            else
            {
                return handle_decimal128_error(decimal128, result, on_error);
            }
        }
        mxb_assert(!true);
        [[fallthrough]];

    case bsoncxx::type::k_double:
        return BsonValue(value.get_double());

    case bsoncxx::type::k_int32:
        return BsonValue((double)value.get_int32());

    case bsoncxx::type::k_int64:
        return BsonValue((double)value.get_int64());

    case bsoncxx::type::k_utf8:
        {
            string_view sv = value.get_utf8();

            if (!sv.empty() && isspace(sv.front()))
            {
                if (nobson::is_null(on_error))
                {
                    stringstream ss;
                    ss << "Failed to parse number '" << sv
                       << "' in $convert with no onError value: Leading whitespace";

                    throw SoftError(ss.str(), error::CONVERSION_FAILURE);
                }

                return BsonValue(on_error);
            }
            else
            {
                errno = 0;

                string s(sv);
                char* pEnd;
                double d = strtod(s.c_str(), &pEnd);

                if (*pEnd != 0)
                {
                    if (nobson::is_null(on_error))
                    {
                        stringstream ss;
                        ss << "Failed to parse number '" << sv
                           << "' in $convert with no onError value: Did not consume whole string.";

                        throw SoftError(ss.str(), error::CONVERSION_FAILURE);
                    }

                    return BsonValue(on_error);
                }

                if (errno == ERANGE)
                {
                    if (nobson::is_null(on_error))
                    {
                        stringstream ss;
                        ss << "Failed to parse number '" << sv
                           << "' in $convert with no onError value: Out of range";

                        throw SoftError(ss.str(), error::CONVERSION_FAILURE);
                    }

                    return BsonValue(on_error);
                }

                return BsonValue(d);
            }
        }
        mxb_assert(!true);

    default:
        break;
    }

    return handle_default_case(value.type(), bsoncxx::type::k_double, on_error);
}

//static
bsoncxx::types::bson_value::value Convert::to_int32(const BsonView& value, const BsonView& on_error)
{
    switch (value.type())
    {
    case bsoncxx::type::k_bool:
        return BsonValue((int32_t)value.get_bool());

    case bsoncxx::type::k_decimal128:
        {
            bsoncxx::decimal128 decimal128 = value.get_decimal128().value;
            int32_t i;
            auto result = nobson::convert(decimal128, &i);

            if (result == nobson::ConversionResult::OK)
            {
                return BsonValue(i);
            }
            else
            {
                return handle_decimal128_error(decimal128, result, on_error);
            }
        }
        mxb_assert(!true);
        [[fallthrough]];

    case bsoncxx::type::k_double:
        return BsonValue((int32_t)value.get_double());

    case bsoncxx::type::k_int32:
        return BsonValue(value.get_int32());

    case bsoncxx::type::k_int64:
        {
            int64_t v = value.get_int64();

            if (v < std::numeric_limits<int32_t>::min())
            {
                if (nobson::is_null(on_error))
                {
                    stringstream ss;
                    ss << "Conversion would underflow target type in $convert with no onError value: "
                       << v;

                    throw SoftError(ss.str(), error::CONVERSION_FAILURE);
                }

                return BsonValue(on_error);
            }
            else if (v > std::numeric_limits<int32_t>::max())
            {
                if (nobson::is_null(on_error))
                {
                    stringstream ss;
                    ss << "Conversion would overflow target type in $convert with no onError value: "
                       << v;

                    throw SoftError(ss.str(), error::CONVERSION_FAILURE);
                }

                return BsonValue(on_error);
            }
            else
            {
                return BsonValue((int32_t)v);
            }
        }
        mxb_assert(!true);
        [[fallthrough]];

    case bsoncxx::type::k_utf8:
        {
            string_view sv = value.get_utf8();

            if (!sv.empty() && isspace(sv.front()))
            {
                if (nobson::is_null(on_error))
                {
                    stringstream ss;
                    ss << "Failed to parse number '" << sv
                       << "' in $convert with no onError value: Leading whitespace";

                    throw SoftError(ss.str(), error::CONVERSION_FAILURE);
                }

                return BsonValue(on_error);
            }
            else
            {
                errno = 0;

                string s(sv);
                char* pEnd;
                long l = strtol(s.c_str(), &pEnd, 10);

                if (*pEnd != 0)
                {
                    if (nobson::is_null(on_error))
                    {
                        stringstream ss;
                        ss << "Failed to parse number '" << sv
                           << "' in $convert with no onError value: Did not consume whole string.";

                        throw SoftError(ss.str(), error::CONVERSION_FAILURE);
                    }

                    return BsonValue(on_error);
                }
                else
                {
                    if (l < std::numeric_limits<int32_t>::min() || l > std::numeric_limits<int32_t>::max())
                    {
                        errno = ERANGE;
                    }

                    if (errno == ERANGE)
                    {
                        if (nobson::is_null(on_error))
                        {
                            stringstream ss;
                            ss << "Failed to parse number '" << sv
                               << "' in $convert with no onError value: Out of range";

                            throw SoftError(ss.str(), error::CONVERSION_FAILURE);
                        }

                        return BsonValue(on_error);
                    }
                    else
                    {
                        return BsonValue((int32_t)l);
                    }
                }
            }
        }
        mxb_assert(!true);
        [[fallthrough]];

    default:
        break;
    }

    return handle_default_case(value.type(), bsoncxx::type::k_int32, on_error);
}

//static
bsoncxx::types::bson_value::value Convert::to_int64(const BsonView& value, const BsonView& on_error)
{
    switch (value.type())
    {
    case bsoncxx::type::k_bool:
        return BsonValue((int64_t)value.get_bool());

    case bsoncxx::type::k_date:
        return BsonValue(value.get_date().to_int64());

    case bsoncxx::type::k_decimal128:
        {
            bsoncxx::decimal128 decimal128 = value.get_decimal128().value;
            int64_t i;
            auto result = nobson::convert(decimal128, &i);

            if (result == nobson::ConversionResult::OK)
            {
                return BsonValue(i);
            }
            else
            {
                return handle_decimal128_error(decimal128, result, on_error);
            }
        }
        mxb_assert(!true);
        [[fallthrough]];

    case bsoncxx::type::k_double:
        return BsonValue((int64_t)value.get_double());

    case bsoncxx::type::k_int32:
        return BsonValue((int64_t)value.get_int32());

    case bsoncxx::type::k_int64:
        return BsonValue(value.get_int64());

    case bsoncxx::type::k_utf8:
        {
            string_view sv = value.get_utf8();

            if (!sv.empty() && isspace(sv.front()))
            {
                if (nobson::is_null(on_error))
                {
                    stringstream ss;
                    ss << "Failed to parse number '" << sv
                       << "' in $convert with no onError value: Leading whitespace";

                    throw SoftError(ss.str(), error::CONVERSION_FAILURE);
                }

                return BsonValue(on_error);
            }
            else
            {
                errno = 0;

                string s(sv);
                char* pEnd;
                long l = strtol(s.c_str(), &pEnd, 10);

                if (*pEnd != 0)
                {
                    if (nobson::is_null(on_error))
                    {
                        stringstream ss;
                        ss << "Failed to parse number '" << sv
                           << "' in $convert with no onError value: Did not consume whole string.";

                        throw SoftError(ss.str(), error::CONVERSION_FAILURE);
                    }

                    return BsonValue(on_error);
                }
                else if (errno == ERANGE)
                {
                    if (nobson::is_null(on_error))
                    {
                        stringstream ss;
                        ss << "Failed to parse number '" << sv
                           << "' in $convert with no onError value: Out of range";

                        throw SoftError(ss.str(), error::CONVERSION_FAILURE);
                    }

                    return BsonValue(on_error);
                }
                else
                {
                    return BsonValue((int64_t)l);
                }
            }
        }
        mxb_assert(!true);
        [[fallthrough]];

    default:
        break;
    }

    return handle_default_case(value.type(), bsoncxx::type::k_int64, on_error);
}

//static
bsoncxx::types::bson_value::value Convert::to_oid(const BsonView& value, const BsonView& on_error)
{
    switch (value.type())
    {
    case bsoncxx::type::k_oid:
        return value.get_oid();

    case bsoncxx::type::k_utf8:
        {
            string_view sv = value.get_utf8();

            if (sv.length() != 24)
            {
                if (nobson::is_null(on_error))
                {
                    stringstream ss;
                    ss << "Failed to parse objectId '" << sv << "' in $convert with no onError value: "
                       << "Invalid string length for parsing to OID, expected 24 but found "
                       << sv.length();

                    throw SoftError(ss.str(), error::CONVERSION_FAILURE);
                }

                return BsonValue(on_error);
            }

            return BsonValue(bsoncxx::oid(sv));
        }
        mxb_assert(!true);
        [[fallthrough]];

    default:
        break;
    }

    return handle_default_case(value.type(), bsoncxx::type::k_oid, on_error);
}

//static
bsoncxx::types::bson_value::value Convert::to_string(const BsonView& value, const BsonView& on_error)
{
    stringstream ss;

    switch (value.type())
    {
    case bsoncxx::type::k_bool:
        ss << (value.get_bool() ? "true" : "false");
        break;

    case bsoncxx::type::k_double:
        ss << value.get_double();
        break;

    case bsoncxx::type::k_decimal128:
        ss << value.get_decimal128().value.to_string();
        break;

    case bsoncxx::type::k_int32:
        ss << value.get_int32();
        break;

    case bsoncxx::type::k_int64:
        ss << value.get_int64();
        break;

    case bsoncxx::type::k_oid:
        ss << value.get_oid().value.to_string();
        break;

    case bsoncxx::type::k_utf8:
        ss << value.get_utf8().value;
        break;

    case bsoncxx::type::k_date:
        {
            auto date = value.get_date();
            std::chrono::system_clock::time_point tp = date;
            std::time_t time = std::chrono::system_clock::to_time_t(tp);
            auto duration = tp.time_since_epoch();
            auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(duration) % 1000;

            std::tm tm;
            gmtime_r(&time, &tm);

            ss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S")
               << "." << std::setw(3) << std::setfill('0') << millis.count() << 'Z';
        }
        break;

    default:
        return handle_default_case(value.type(), bsoncxx::type::k_utf8, on_error);
    }

    return BsonValue(ss.str());
}

//static
bsoncxx::types::bson_value::value Convert::to_minkey(const BsonView& value, const BsonView& on_error)
{
    return handle_default_case(value.type(), bsoncxx::type::k_minkey, on_error);
}

//static
Convert::Converter Convert::get_converter(const bsoncxx::types::bson_value::view& v)
{
    Converter rv;

    auto type = v.type();

    if (nobson::is_integer(type))
    {
        rv = get_converter(static_cast<bsoncxx::type>(nobson::get_integer<int32_t>(v)));
    }
    else if (nobson::is_string(type))
    {
        rv = get_converter(v.get_string());
    }
    else if (nobson::is_null(type) || nobson::is_undefined(type))
    {
        rv = [](const BsonView& value, const BsonView& on_error) {
            return BsonValue(nullptr);
        };
    }
    else
    {
        stringstream serr;
        serr << "$convert's 'to' argument must be a string or number, but is " << bsoncxx::to_string(type);

        throw SoftError(serr.str(), error::FAILED_TO_PARSE);
    }

    return rv;
}

namespace
{

template<bsoncxx::type type>
Convert::BsonValue to_unsupported_type(const Convert::BsonView& value, const Convert::BsonView& on_error)
{
    return Convert::handle_default_case(value.type(), type, on_error);
}

}

//static
Convert::Converter Convert::get_converter(bsoncxx::type type)
{
    Converter c;

    switch (type)
    {
        // Supported
    case bsoncxx::type::k_bool:
        c = to_bool;
        break;

    case bsoncxx::type::k_date:
        c = to_date;
        break;

    case bsoncxx::type::k_decimal128:
        c = to_decimal;
        break;

    case bsoncxx::type::k_double:
        c = to_double;
        break;

    case bsoncxx::type::k_int32:
        c = to_int32;
        break;

    case bsoncxx::type::k_int64:
        c = to_int64;
        break;

    case bsoncxx::type::k_oid:
        c = to_oid;
        break;

    case bsoncxx::type::k_string:
        c = to_string;
        break;

        // Unsupported
    case bsoncxx::type::k_array:
        c = to_unsupported_type<bsoncxx::type::k_array>;
        break;

    case bsoncxx::type::k_binary:
        c = to_unsupported_type<bsoncxx::type::k_binary>;
        break;

    case bsoncxx::type::k_code:
        c = to_unsupported_type<bsoncxx::type::k_code>;
        break;

    case bsoncxx::type::k_codewscope:
        c = to_unsupported_type<bsoncxx::type::k_codewscope>;
        break;

    case bsoncxx::type::k_dbpointer:
        c = to_unsupported_type<bsoncxx::type::k_dbpointer>;
        break;

    case bsoncxx::type::k_document:
        c = to_unsupported_type<bsoncxx::type::k_document>;
        break;

    case bsoncxx::type::k_minkey:
        c = to_unsupported_type<bsoncxx::type::k_minkey>;
        break;

    case bsoncxx::type::k_maxkey:
        c = to_unsupported_type<bsoncxx::type::k_maxkey>;
        break;

    case bsoncxx::type::k_null:
        c = to_unsupported_type<bsoncxx::type::k_null>;
        break;

    case bsoncxx::type::k_regex:
        c = to_unsupported_type<bsoncxx::type::k_regex>;
        break;

    case bsoncxx::type::k_symbol:
        c = to_unsupported_type<bsoncxx::type::k_symbol>;
        break;

    case bsoncxx::type::k_timestamp:
        c = to_unsupported_type<bsoncxx::type::k_timestamp>;
        break;

    case bsoncxx::type::k_undefined:
        c = to_unsupported_type<bsoncxx::type::k_undefined>;
        break;

    default:
        {
            stringstream ss;
            ss << "In $convert, numeric value for 'to' does not correspond to a BSON type: "
               << static_cast<int32_t>(type);

            throw SoftError(ss.str(), error::FAILED_TO_PARSE);
        }
    }

    return c;
}

//static
Convert::Converter Convert::get_converter(std::string_view type)
{
    Converter converter;

    auto it = type_codes_by_name.find(type);

    if (it != type_codes_by_name.end())
    {
        converter = get_converter(it->second);
    }
    else
    {
        converter = [name = string(type)](const BsonView& value, const BsonView& on_error) {
            if (nobson::is_null(on_error))
            {
                stringstream serr;
                serr << "Unknown type name: " << name;

                throw SoftError(serr.str(), error::BAD_VALUE);
            }

            return BsonValue(on_error);

        };
    }

    return converter;
}

//static
bsoncxx::types::bson_value::value Convert::handle_decimal128_error(bsoncxx::decimal128 decimal128,
                                                                   nobson::ConversionResult result,
                                                                   const BsonView& on_error)
{
    if (nobson::is_null(on_error))
    {
        if (result == nobson::ConversionResult::OVERFLOW)
        {
            stringstream ss;
            ss << "Conversion would overflow target type in $convert with no onError value: "
               << decimal128.to_string();

            throw SoftError(ss.str(), error::CONVERSION_FAILURE);
        }
        else
        {
            mxb_assert(result == nobson::ConversionResult::UNDERFLOW);

            stringstream ss;
            ss << "Conversion would underflow target type in $convert with no onError value: "
               << decimal128.to_string();

            throw SoftError(ss.str(), error::CONVERSION_FAILURE);
        }
    }

    return BsonValue(on_error);
}

//static
bsoncxx::types::bson_value::value Convert::handle_default_case(bsoncxx::type from,
                                                               bsoncxx::type to,
                                                               const BsonView& on_error)
{
    if (nobson::is_null(on_error))
    {
        stringstream ss;
        ss << "Unsupported conversion from "
           << bsoncxx::to_string(from) << " to "
           << bsoncxx::to_string(to) << " in $convert with no onError value";

        throw SoftError(ss.str(), error::CONVERSION_FAILURE);
    }

    return BsonValue(on_error);
}

/**
 * Divide
 */
bsoncxx::types::bson_value::value Divide::process(bsoncxx::document::view doc)
{
    mxb_assert(m_ops.size() == 2);

    auto lhs = m_ops[0]->process(doc);
    auto rhs = m_ops[1]->process(doc);

    const auto approach = nobson::NumberApproach::REJECT_DECIMAL128;
    if (!nobson::is_number(lhs, approach) || !nobson::is_number(rhs, approach))
    {
        stringstream ss;
        ss << "$divide only supports numeric types, not "
           << bsoncxx::to_string(lhs.view().type()) << " and " << bsoncxx::to_string(rhs.view().type());

        throw SoftError(ss.str(), error::TYPE_MISMATCH);
    }

    if (nobson::is_zero(rhs))
    {
        throw SoftError("can't $divide by zero", error::LOCATION16608);
    }

    return nobson::div(lhs, rhs);
}

/**
 * Eq
 */
bsoncxx::types::bson_value::value Eq::process(bsoncxx::document::view doc)
{
    mxb_assert(m_ops.size() == 2);

    auto lhs = m_ops[0]->process(doc);
    auto rhs = m_ops[1]->process(doc);

    return BsonValue(lhs == rhs);
}

/**
 * Exp
 */
bsoncxx::types::bson_value::value Exp::process(bsoncxx::document::view doc)
{
    auto value = m_sOp->process(doc);

    if (!nobson::is_number(value, nobson::NumberApproach::REJECT_DECIMAL128))
    {
        stringstream ss;
        ss << "$exp only supports numeric types, not " << bsoncxx::to_string(value.view().type());

        throw SoftError(ss.str(), error::LOCATION28765);
    }

    return nobson::exp(value);
}

/**
 * First
 */
bsoncxx::types::bson_value::value First::process(bsoncxx::document::view doc)
{
    bool null_is_ok;
    return process(doc, &null_is_ok);
}

void First::append(DocumentBuilder& builder, std::string_view key, bsoncxx::document::view doc)
{
    bool null_is_ok;

    auto value = process(doc, &null_is_ok);

    if (value.view().type() != bsoncxx::type::k_null || null_is_ok)
    {
        Base::append(builder, key, value);
    }
}

bsoncxx::types::bson_value::value First::process(bsoncxx::document::view doc, bool* pNull_is_ok)
{
    *pNull_is_ok = false;

    BsonValue avalue = m_sOp->process(doc);
    BsonView aview = avalue.view();
    auto type = aview.type();

    if (type == bsoncxx::type::k_null || type == bsoncxx::type::k_undefined)
    {
        *pNull_is_ok = true;
        return BsonValue(nullptr);
    }

    if (type != bsoncxx::type::k_array)
    {
        stringstream serr;
        serr << "$first's argument must be an array, but is " << bsoncxx::to_string(type);

        throw SoftError(serr.str(), error::LOCATION28689);
    }

    return ArrayElemAt::access(aview.get_array(), 0);
}

/**
 * Floor
 */
bsoncxx::types::bson_value::value Floor::process(bsoncxx::document::view doc)
{
    auto value = m_sOp->process(doc);

    if (!nobson::is_null(value))
    {
        if (!nobson::is_number(value, nobson::NumberApproach::REJECT_DECIMAL128))
        {
            stringstream ss;
            ss << "$floor only supports numeric types, not " << bsoncxx::to_string(value.view().type());

            throw SoftError(ss.str(), error::LOCATION28765);
        }

        value = nobson::floor(value);
    }

    return value;
}

/**
 * Gt
 */
bsoncxx::types::bson_value::value Gt::process(bsoncxx::document::view doc)
{
    mxb_assert(m_ops.size() == 2);

    BsonView lhs = m_ops[0]->process(doc);
    BsonView rhs = m_ops[1]->process(doc);

    return lhs > rhs;
}

/**
 * Gte
 */
bsoncxx::types::bson_value::value Gte::process(bsoncxx::document::view doc)
{
    mxb_assert(m_ops.size() == 2);

    BsonView lhs = m_ops[0]->process(doc);
    BsonView rhs = m_ops[1]->process(doc);

    return lhs >= rhs;
}

/**
 * IfNull
 */
bsoncxx::types::bson_value::value IfNull::process(bsoncxx::document::view doc)
{
    mxb_assert(m_ops.size() == 2);

    auto condition = m_ops[0]->process(doc);

    return nobson::is_null(condition) || nobson::is_undefined(condition) ? m_ops[1]->process(doc) : condition;
}

/**
 * IsArray
 */
bsoncxx::types::bson_value::value IsArray::process(bsoncxx::document::view doc)
{
    auto value = m_sOp->process(doc);

    return BsonValue(value.view().type() == bsoncxx::type::k_array ? true : false);
}

/**
 * IsNumber
 */
bsoncxx::types::bson_value::value IsNumber::process(bsoncxx::document::view doc)
{
    auto value = m_sOp->process(doc);

    bool rv = false;

    switch (value.view().type())
    {
    case bsoncxx::type::k_int32:
    case bsoncxx::type::k_int64:
    case bsoncxx::type::k_double:
    case bsoncxx::type::k_decimal128:
        rv = true;
        break;

    default:
        ;
    }

    return BsonValue(rv);
}

/**
 * Last
 */
bsoncxx::types::bson_value::value Last::process(bsoncxx::document::view doc)
{
    bool null_is_ok;
    return process(doc, &null_is_ok);
}

void Last::append(DocumentBuilder& builder, std::string_view key, bsoncxx::document::view doc)
{
    bool null_is_ok;

    auto value = process(doc, &null_is_ok);

    if (value.view().type() != bsoncxx::type::k_null || null_is_ok)
    {
        Base::append(builder, key, value);
    }
}

bsoncxx::types::bson_value::value Last::process(bsoncxx::document::view doc, bool* pNull_is_ok)
{
    *pNull_is_ok = false;

    BsonValue avalue = m_sOp->process(doc);
    BsonView aview = avalue.view();
    auto type = aview.type();

    if (type == bsoncxx::type::k_null || type == bsoncxx::type::k_undefined)
    {
        *pNull_is_ok = true;
        return BsonValue(nullptr);
    }

    if (type != bsoncxx::type::k_array)
    {
        stringstream serr;
        serr << "$last's argument must be an array, but is " << bsoncxx::to_string(type);

        throw SoftError(serr.str(), error::LOCATION28689);
    }

    return ArrayElemAt::access(aview.get_array(), -1);
}

/**
 * Literal
 */
Literal::Literal(const BsonView& value)
    : m_value(value)
{
}

bsoncxx::types::bson_value::value Literal::process(bsoncxx::document::view doc)
{
    return m_value;
}

/**
 * Ln
 */
bsoncxx::types::bson_value::value Ln::process(bsoncxx::document::view doc)
{
    auto value = m_sOp->process(doc);

    if (!nobson::is_null(value))
    {
        if (!nobson::is_number(value, nobson::NumberApproach::REJECT_DECIMAL128))
        {
            stringstream ss;
            ss << "$ln only supports numeric types, not " << bsoncxx::to_string(value.view().type());

            throw SoftError(ss.str(), error::LOCATION28765);
        }
    }

    return nobson::is_null(value) ? value : nobson::log(value);
}

/**
 * Log
 */
bsoncxx::types::bson_value::value Log::process(bsoncxx::document::view doc)
{
    mxb_assert(m_ops.size() == 2);

    auto number = m_ops[0]->process(doc);
    auto base = m_ops[1]->process(doc);

    if (!nobson::is_number(number, nobson::NumberApproach::REJECT_DECIMAL128))
    {
        stringstream ss;
        ss << "$log's argument must be numeric, not " << bsoncxx::to_string(number.view().type());

        throw SoftError(ss.str(), error::LOCATION28756);
    }

    if (!nobson::is_number(base, nobson::NumberApproach::REJECT_DECIMAL128))
    {
        stringstream ss;
        ss << "$log's base must be numeric, not " << bsoncxx::to_string(base.view().type());

        throw SoftError(ss.str(), error::LOCATION28757);
    }

    if (number <= BsonValue((int32_t)0))
    {
        stringstream ss;
        ss << "$log's argument must be a positive number, but is " << nobson::to_bson_expression(number);

        throw SoftError(ss.str(), error::LOCATION28758);
    }

    if (base <= BsonValue((int32_t)0) || base == BsonValue((int32_t)1))
    {
        stringstream ss;
        ss << "$log's base must be a positive number not equal to 1, but is "
           << nobson::to_bson_expression(base);

        throw SoftError(ss.str(), error::LOCATION28759);
    }

    return nobson::div(nobson::log(number), nobson::log(base));
}

/**
 * Log10
 */
bsoncxx::types::bson_value::value Log10::process(bsoncxx::document::view doc)
{
    auto number = m_sOp->process(doc);

    if (!nobson::is_number(number, nobson::NumberApproach::REJECT_DECIMAL128))
    {
        stringstream ss;
        ss << "$log10 must be a positive number, but is " << bsoncxx::to_string(number.view().type());

        throw SoftError(ss.str(), error::LOCATION28765);
    }

    if (number <= BsonValue((int32_t)0))
    {
        stringstream ss;
        ss << "$log10's argument must be a positive number, but is " << nobson::to_bson_expression(number);

        throw SoftError(ss.str(), error::LOCATION28761);
    }

    return nobson::div(nobson::log(number), nobson::log(BsonValue((int32_t)10)));
}

/**
 * Lt
 */
bsoncxx::types::bson_value::value Lt::process(bsoncxx::document::view doc)
{
    mxb_assert(m_ops.size() == 2);

    auto lhs = m_ops[0]->process(doc);
    auto rhs = m_ops[1]->process(doc);

    return lhs < rhs;
}

/**
 * Lte
 */
bsoncxx::types::bson_value::value Lte::process(bsoncxx::document::view doc)
{
    mxb_assert(m_ops.size() == 2);

    auto lhs = m_ops[0]->process(doc);
    auto rhs = m_ops[1]->process(doc);

    return lhs <= rhs;
}

/**
 * Mod
 */
bsoncxx::types::bson_value::value Mod::process(bsoncxx::document::view doc)
{
    mxb_assert(m_ops.size() == 2);

    auto lhs = m_ops[0]->process(doc);
    auto rhs = m_ops[1]->process(doc);

    const auto approach = nobson::NumberApproach::REJECT_DECIMAL128;
    if (!nobson::is_number(lhs, approach) || !nobson::is_number(rhs, approach))
    {
        stringstream ss;
        ss << "$mod only supports numeric types, not "
           << bsoncxx::to_string(lhs.view().type()) << " and " << bsoncxx::to_string(rhs.view().type());

        throw SoftError(ss.str(), error::LOCATION16611);
    }

    if (nobson::is_zero(rhs))
    {
        throw SoftError("can't $mod by zero", error::LOCATION16610);
    }

    return nobson::mod(lhs, rhs);
}

/**
 * Multiply
 */
bsoncxx::types::bson_value::value Multiply::process(bsoncxx::document::view doc)
{
    BsonValue rv(nullptr);

    for (auto& sOp : m_ops)
    {
        auto value = sOp->process(doc);

        if (nobson::is_number(value, nobson::NumberApproach::REJECT_DECIMAL128))
        {
            if (nobson::is_null(rv))
            {
                rv = value;
            }
            else
            {
                rv = nobson::mul(rv, value);
            }
        }
    }

    return rv;
}

/**
 * Ne
 */
bsoncxx::types::bson_value::value Ne::process(bsoncxx::document::view doc)
{
    mxb_assert(m_ops.size() == 2);

    auto lhs = m_ops[0]->process(doc);
    auto rhs = m_ops[1]->process(doc);

    return BsonValue(lhs != rhs);
}

/**
 * Not
 */
Not::Not(const BsonView& value)
{
    if (value.type() == bsoncxx::type::k_array)
    {
        bsoncxx::array::view array = value.get_array();

        int32_t n = 0;
        for (auto it = array.begin(); it != array.end(); ++it)
        {
            ++n;

            m_sOp = Operator::create(it->get_value());
        }

        if (n != 1)
        {
            stringstream ss;
            ss << "not takes exactly 1 arguments. " << n << " were passed in.";

            throw SoftError(ss.str(), error::LOCATION16020);
        }
    }
    else
    {
        m_sOp = Operator::create(value);
    }
}

bsoncxx::types::bson_value::value Not::process(bsoncxx::document::view doc)
{
    auto value = m_sOp->process(doc);

    return BsonValue(nobson::is_truthy(value) ? false : true);
}

/**
 * Or
 */
bsoncxx::types::bson_value::value Or::process(bsoncxx::document::view doc)
{
    bool rv = false;

    for (const auto& sOp : m_ops)
    {
        rv = nobson::is_truthy(sOp->process(doc));

        if (rv)
        {
            break;
        }
    }

    return bsoncxx::types::bson_value::value(rv);
}

/**
 * Pow
 */
bsoncxx::types::bson_value::value Pow::process(bsoncxx::document::view doc)
{
    mxb_assert(m_ops.size() == 2);

    auto base = m_ops[0]->process(doc);
    auto exponent = m_ops[1]->process(doc);

    if (!nobson::is_number(base, nobson::NumberApproach::REJECT_DECIMAL128))
    {
        stringstream ss;
        ss << "$pow's base must be numeric, not " << bsoncxx::to_string(base.view().type());

        throw SoftError(ss.str(), error::LOCATION28762);
    }

    if (!nobson::is_number(exponent, nobson::NumberApproach::REJECT_DECIMAL128))
    {
        stringstream ss;
        ss << "$pow's exponent must be numeric, not " << bsoncxx::to_string(exponent.view().type());

        throw SoftError(ss.str(), error::LOCATION28763);
    }

    if (base == BsonValue((int32_t)0) && exponent < BsonValue((int32_t)0))
    {
        stringstream ss;
        ss << "$pow cannot take a base of 0 and a negative exponent";

        throw SoftError(ss.str(), error::LOCATION28764);
    }

    return nobson::pow(base, exponent);
}

/**
 * Sqrt
 */
bsoncxx::types::bson_value::value Sqrt::process(bsoncxx::document::view doc)
{
    auto value = m_sOp->process(doc);

    if (!nobson::is_null(value))
    {
        if (!nobson::is_number(value, nobson::NumberApproach::REJECT_DECIMAL128))
        {
            stringstream ss;
            ss << "$sqrt only supports numeric types, not " << bsoncxx::to_string(value.view().type());

            throw SoftError(ss.str(), error::LOCATION28765);
        }

        if (value < BsonValue((int32_t)0))
        {
            stringstream ss;
            ss << "$sqrt's argument must be greater than or equal to 0";

            throw SoftError(ss.str(), error::LOCATION28714);
        }
    }

    return nobson::is_null(value) ? value : nobson::log(value);
}

/**
 * Size
 */
bsoncxx::types::bson_value::value Size::process(bsoncxx::document::view doc)
{
    auto value = m_sOp->process(doc);
    auto view = value.view();

    auto type = view.type();
    if (type != bsoncxx::type::k_array)
    {
        stringstream ss;
        ss << "The argument to $size must be an array, but was of type: " << bsoncxx::to_string(type);

        throw SoftError(ss.str(), error::LOCATION17124);
    }

    bsoncxx::array::view array = view.get_array();

    int32_t n = std::distance(array.begin(), array.end());

    return BsonValue(n);
}

/**
 * Subtract
 */
bsoncxx::types::bson_value::value Subtract::process(bsoncxx::document::view doc)
{
    mxb_assert(m_ops.size() == 2);

    auto lhs = m_ops[0]->process(doc);
    auto rhs = m_ops[1]->process(doc);

    auto approach = nobson::NumberApproach::REJECT_DECIMAL128;
    if (!nobson::is_number(lhs, approach) || !nobson::is_number(rhs, approach))
    {
        stringstream ss;

        if (lhs.view().type() == bsoncxx::type::k_date && nobson::is_number(rhs, approach))
        {
            ss << "Cannot yet subtract from dates.";
            throw SoftError(ss.str(), error::INTERNAL_ERROR);
        }
        else
        {
            ss << "can't $subtract " << bsoncxx::to_string(lhs.view().type())
               << " from " << bsoncxx::to_string(rhs.view().type());

            throw SoftError(ss.str(), error::TYPE_MISMATCH);
        }
    }

    return nobson::sub(lhs, rhs);
}

/**
 * Switch
 */
Switch::Switch(const BsonView& value)
{
    auto type = value.type();

    if (type != bsoncxx::type::k_document)
    {
        stringstream serr;
        serr << "$switch requires an object as an argument, found: "
             << bsoncxx::to_string(type);

        throw SoftError(serr.str(), error::LOCATION40060);
    }

    bsoncxx::document::view s = value.get_document();

    for (bsoncxx::document::element e : static_cast<bsoncxx::document::view>(value.get_document()))
    {
        if (e.key() == "branches")
        {
            type = e.type();
            if (type != bsoncxx::type::k_array)
            {
                stringstream serr;
                serr << "$switch expected an array for 'branches', found: "
                     << bsoncxx::to_string(type);

                throw SoftError(serr.str(), error::LOCATION40061);
            }

            bsoncxx::array::view branches = e.get_array();

            for (bsoncxx::array::element branch : branches)
            {
                type = branch.type();
                if (type != bsoncxx::type::k_document)
                {
                    stringstream serr;
                    serr << "$switch expected each branch to be an object, found: "
                         << bsoncxx::to_string(type);

                    throw SoftError(serr.str(), error::LOCATION40062);
                }

                m_branches.emplace_back(create_branch(branch.get_document()));
            }
        }
        else if (e.key() == "default")
        {
            m_sDefault = Operator::create(e.get_value());
        }
        else
        {
            stringstream serr;
            serr << "$switch found an unknown argument: " << e.key();

            throw SoftError(serr.str(), error::LOCATION40067);
        }
    }

    if (m_branches.empty())
    {
        throw SoftError("$switch requires at least one branch", error::LOCATION40068);
    }
}

bsoncxx::types::bson_value::value Switch::process(bsoncxx::document::view doc)
{
    for (Branch& branch : m_branches)
    {
        if (branch.check(doc))
        {
            return branch.execute(doc);
        }
    }

    if (!m_sDefault)
    {
        throw SoftError("Cannot execute a switch statement where all the cases "
                        "evaluate to false without a default", error::LOCATION40069);
    }

    return m_sDefault->process(doc);
}

void Switch::append(DocumentBuilder& builder,
                    std::string_view key,
                    bsoncxx::document::view doc)
{
    for (Branch& branch : m_branches)
    {
        if (branch.check(doc))
        {
            branch.append(builder, key, doc);
            return;
        }
    }

    if (!m_sDefault)
    {
        throw SoftError("Cannot execute a switch statement where all the cases "
                        "evaluate to false without a default", error::LOCATION40069);
    }

    m_sDefault->append(builder, key, doc);
}

Switch::Branch Switch::create_branch(bsoncxx::document::view branch)
{
    unique_ptr<Operator> sCase;
    unique_ptr<Operator> sThen;

    for (bsoncxx::document::element& e : branch)
    {
        if (e.key() == "case")
        {
            sCase = Operator::create(e.get_value());
        }
        else if (e.key() == "then")
        {
            sThen = Operator::create(e.get_value());
        }
        else
        {
            stringstream serr;
            serr << "$switch found an unknown argument to a branch: " << e.key();

            throw SoftError(serr.str(), error::LOCATION40063);
        }
    }

    if (!sCase)
    {
        throw SoftError("$switch requires each branch have a 'case' expression", error::LOCATION40064);
    }

    if (!sThen)
    {
        throw SoftError("$switch requires each branch have a 'then' expression", error::LOCATION40065);
    }

    return Branch (std::move(sCase), std::move(sThen));
}

/**
 * ToBool
 */
bsoncxx::types::bson_value::value ToBool::process(bsoncxx::document::view doc)
{
    return Convert::to_bool(m_sOp->process(doc));
}

/**
 * ToDate
 */
bsoncxx::types::bson_value::value ToDate::process(bsoncxx::document::view doc)
{
    return Convert::to_date(m_sOp->process(doc));
}

/**
 * ToDecimal
 */
bsoncxx::types::bson_value::value ToDecimal::process(bsoncxx::document::view doc)
{
    return Convert::to_decimal(m_sOp->process(doc));
}

/**
 * ToDouble
 */
bsoncxx::types::bson_value::value ToDouble::process(bsoncxx::document::view doc)
{
    return Convert::to_double(m_sOp->process(doc));
}

/**
 * ToInt
 */
bsoncxx::types::bson_value::value ToInt::process(bsoncxx::document::view doc)
{
    return Convert::to_int32(m_sOp->process(doc));
}

/**
 * ToLong
 */
bsoncxx::types::bson_value::value ToLong::process(bsoncxx::document::view doc)
{
    return Convert::to_int64(m_sOp->process(doc));
}

/**
 * ToObjectId
 */
bsoncxx::types::bson_value::value ToObjectId::process(bsoncxx::document::view doc)
{
    return Convert::to_oid(m_sOp->process(doc));
}

/**
 * ToString
 */
bsoncxx::types::bson_value::value ToString::process(bsoncxx::document::view doc)
{
    return Convert::to_string(m_sOp->process(doc));
}

/**
 * Type
 */
bsoncxx::types::bson_value::value Type::process(bsoncxx::document::view doc)
{
    auto type = m_sOp->process(doc).view().type();

    auto it = type_names_by_code.find(type);
    mxb_assert(it != type_names_by_code.end());

    return BsonValue(it->second);
}

}

}
