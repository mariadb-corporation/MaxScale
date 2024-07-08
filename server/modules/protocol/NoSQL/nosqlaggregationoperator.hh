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
#pragma once

#include "nosqlprotocol.hh"
#include <variant>
#include <bsoncxx/types/bson_value/value.hpp>
#include <bsoncxx/types/bson_value/view.hpp>
#include <maxbase/json.hh>
#include "nosqlbase.hh"
#include "nosqlnobson.hh"

namespace nosql
{

namespace aggregation
{

/**
 * Operator
 */
class Operator
{
public:
    using Creator = std::unique_ptr<Operator>(*)(bsoncxx::types::value);
    using BsonValue = bsoncxx::types::bson_value::value;
    using BsonView = bsoncxx::types::bson_value::view;
    class Accessor;
    class Literal;

    virtual ~Operator();

    static void unsupported(string_view key);

    static std::unique_ptr<Operator> create(BsonView value);

    static std::unique_ptr<Operator> create_expression_operator(BsonView value);

    bool ready() const
    {
        return m_ready;
    }

    virtual BsonValue process(bsoncxx::document::view doc) = 0;

    const BsonValue& value() const
    {
        return m_value;
    }

    using Number = std::variant<int32_t, int64_t, double>;

    static Number mul(const Number& lhs, const Number& rhs);

protected:
    Operator()
        : m_value(nullptr)
    {
    }

    Operator(const BsonView& value)
        : m_value(value)
    {
    }

    void set_ready()
    {
        m_ready = true;
    }

    BsonValue m_value;

private:
    bool m_ready { false };
};

template<class Derived>
class ConcreteOperator : public Operator
{
public:
    using Operator::Operator;

    static std::unique_ptr<Operator> create(BsonView value)
    {
        return std::make_unique<Derived>(value);
    }
};

/**
 * Operator::Accessor
 */
class Operator::Accessor : public ConcreteOperator<Operator::Accessor>
{
public:
    Accessor(BsonView value);

    BsonValue process(bsoncxx::document::view doc) override;

private:
    std::vector<std::string> m_fields;
};

/**
 * Operator::Literal
 */
class Operator::Literal : public ConcreteOperator<Operator::Literal>
{
public:
    Literal(BsonView value);

    BsonValue process(bsoncxx::document::view doc) override;
};

/**
 * Cond
 */
class Cond : public ConcreteOperator<Cond>
{
public:
    static constexpr const char* const NAME = "$cond";

    Cond(BsonView value);

    BsonValue process(bsoncxx::document::view doc) override;

private:
    std::vector<std::unique_ptr<Operator>> m_ops;
};

/**
 * Convert
 */
class Convert : public ConcreteOperator<Convert>
{
public:
    static constexpr const char* const NAME = "$convert";

    Convert(BsonView value);

    BsonValue process(bsoncxx::document::view doc) override;

    static BsonValue to_bool(BsonView value, BsonView on_error = BsonView());
    static BsonValue to_date(BsonView value, BsonView on_error = BsonView());
    static BsonValue to_decimal(BsonView value, BsonView on_error = BsonView());
    static BsonValue to_double(BsonView value, BsonView on_error = BsonView());
    static BsonValue to_int32(BsonView value, BsonView on_error = BsonView());
    static BsonValue to_int64(BsonView value, BsonView on_error = BsonView());
    static BsonValue to_oid(BsonView value, BsonView on_error = BsonView());
    static BsonValue to_string(BsonView value, BsonView on_error = BsonView());

private:
    using Converter = BsonValue (*)(BsonView value, BsonView on_error);

    static Converter get_converter(bsoncxx::document::element e);
    static Converter get_converter(bsoncxx::type type);
    static Converter get_converter(std::string_view type);

    static BsonValue handle_decimal128_error(bsoncxx::decimal128 decimal,
                                             nobson::ConversionResult result,
                                             BsonView on_error);

    static BsonValue handle_default_case(bsoncxx::type from,
                                         bsoncxx::type to,
                                         BsonView on_error);

    std::unique_ptr<Operator> m_sInput;
    Converter                 m_to;
    BsonView     m_on_error;
    BsonView     m_on_null;
};

/**
 * Divide
 */
class Divide : public ConcreteOperator<Divide>
{
public:
    static constexpr const char* const NAME = "$divide";

    Divide(BsonView value);

    BsonValue process(bsoncxx::document::view doc) override;

private:
    std::vector<std::unique_ptr<Operator>> m_ops;
    ArrayBuilder                           m_builder;
};

/**
 * First
 */
class First : public ConcreteOperator<First>
{
public:
    static constexpr const char* const NAME = "$first";

    First(BsonView value);

    BsonValue process(bsoncxx::document::view doc) override;

private:
    Accessor m_field;
};

/**
 * Last
 */
class Last : public ConcreteOperator<Last>
{
public:
    static constexpr const char* const NAME = "$last";

    Last(BsonView value);

    BsonValue process(bsoncxx::document::view doc) override;

private:
    Accessor m_field;
};

/**
 * Max
 */
class Max : public ConcreteOperator<Max>
{
public:
    static constexpr const char* const NAME = "$max";

    Max(BsonView value);

    BsonValue process(bsoncxx::document::view doc) override;

private:
    bool                      m_first { true };
    std::unique_ptr<Operator> m_sOp;
};

/**
 * Min
 */
class Min : public ConcreteOperator<Min>
{
public:
    static constexpr const char* const NAME = "$min";

    Min(BsonView value);

    BsonValue process(bsoncxx::document::view doc) override;

private:
    bool                      m_first { true };
    std::unique_ptr<Operator> m_sOp;
};

/**
 * Multiply
 */
class Multiply : public ConcreteOperator<Multiply>
{
public:
    static constexpr const char* const NAME = "$multiply";

    Multiply(BsonView value);

    BsonValue process(bsoncxx::document::view doc) override;

private:
    std::vector<std::unique_ptr<Operator>> m_ops;
    ArrayBuilder                           m_builder;
};

/**
 * Ne
 */
class Ne : public ConcreteOperator<Ne>
{
public:
    static constexpr const char* const NAME = "$ne";

    Ne(BsonView value);

    BsonValue process(bsoncxx::document::view doc) override;

private:
    std::vector<std::unique_ptr<Operator>> m_ops;
    ArrayBuilder                           m_builder;
};

/**
 * Sum
 */
class Sum : public ConcreteOperator<Sum>
{
public:
    static constexpr const char* const NAME = "$sum";

    Sum(BsonView value);

    BsonValue process(bsoncxx::document::view doc) override;

private:
    void add_int32(int32_t value);
    void add_int64(int64_t value);
    void add_double(double value);

    std::unique_ptr<Operator> m_sOp;
    ArrayBuilder              m_builder;
};

/**
 * ToBool
 */
class ToBool : public ConcreteOperator<ToBool>
{
public:
    static constexpr const char* const NAME = "$toBool";

    ToBool(BsonView value);

    BsonValue process(bsoncxx::document::view doc) override;

private:
    std::unique_ptr<Operator> m_sOp;
    ArrayBuilder              m_builder;
};

/**
 * ToDate
 */
class ToDate : public ConcreteOperator<ToDate>
{
public:
    static constexpr const char* const NAME = "$toDate";

    ToDate(BsonView value);

    BsonValue process(bsoncxx::document::view doc) override;

private:
    std::unique_ptr<Operator> m_sOp;
    ArrayBuilder              m_builder;
};

/**
 * ToDecimal
 */
class ToDecimal : public ConcreteOperator<ToDecimal>
{
public:
    static constexpr const char* const NAME = "$toDecimal";

    ToDecimal(BsonView value);

    BsonValue process(bsoncxx::document::view doc) override;

private:
    std::unique_ptr<Operator> m_sOp;
    ArrayBuilder              m_builder;
};

/**
 * ToDouble
 */
class ToDouble : public ConcreteOperator<ToDouble>
{
public:
    static constexpr const char* const NAME = "$toDouble";

    ToDouble(BsonView value);

    BsonValue process(bsoncxx::document::view doc) override;

private:
    std::unique_ptr<Operator> m_sOp;
    ArrayBuilder              m_builder;
};

/**
 * ToInt
 */
class ToInt : public ConcreteOperator<ToInt>
{
public:
    static constexpr const char* const NAME = "$toInt";

    ToInt(BsonView value);

    BsonValue process(bsoncxx::document::view doc) override;

private:
    std::unique_ptr<Operator> m_sOp;
    ArrayBuilder              m_builder;
};

/**
 * ToLong
 */
class ToLong : public ConcreteOperator<ToLong>
{
public:
    static constexpr const char* const NAME = "$toLong";

    ToLong(BsonView value);

    BsonValue process(bsoncxx::document::view doc) override;

private:
    std::unique_ptr<Operator> m_sOp;
    ArrayBuilder              m_builder;
};

/**
 * ToObjectId
 */
class ToObjectId : public ConcreteOperator<ToObjectId>
{
public:
    static constexpr const char* const NAME = "$toObjectId";

    ToObjectId(BsonView value);

    BsonValue process(bsoncxx::document::view doc) override;

private:
    std::unique_ptr<Operator> m_sOp;
    ArrayBuilder              m_builder;
};

/**
 * ToString
 */
class ToString : public ConcreteOperator<ToString>
{
public:
    static constexpr const char* const NAME = "$toString";

    ToString(BsonView value);

    BsonValue process(bsoncxx::document::view doc) override;

private:
    std::unique_ptr<Operator> m_sOp;
    ArrayBuilder              m_builder;
};

}
}
