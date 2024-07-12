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
#include <set>
#include "nosqloperator.hh"

namespace nosql
{

namespace aggregation
{

/**
 * Operator
 */
class Operator : public nosql::Operator
{
public:
    using Creator = std::unique_ptr<Operator>(*)(const BsonView& value);
    class Accessor;
    class MultiAccessor;

    virtual ~Operator();

    static std::unique_ptr<Operator> create(const BsonView& value);

    virtual BsonValue process(bsoncxx::document::view doc) = 0;

protected:
    Operator()
    {
    }

    using Operators = std::vector<std::unique_ptr<Operator>>;

    static Operators create_operators(const BsonView& value,
                                      const char* zOp,
                                      size_t nMin,
                                      size_t nMax,
                                      const std::set<bsoncxx::type>& types);

private:
    static Operators create_operators(const bsoncxx::array::view& array,
                                      const char* zOp,
                                      size_t nMin,
                                      size_t nMax,
                                      const std::set<bsoncxx::type>& types);

    static std::unique_ptr<Operator> create_operator(const BsonView& value,
                                                     const char* zOp,
                                                     const std::set<bsoncxx::type>& types);
};

template<class DerivedBy>
using ConcreteOperator = nosql::ConcreteOperator<DerivedBy, Operator>;

template<class DerivedBy>
using SingleExpressionOperator = nosql::SingleExpressionOperator<DerivedBy, Operator>;

template<class DerivedBy>
using MultiExpressionOperator = nosql::MultiExpressionOperator<DerivedBy, Operator>;

template<class DerivedBy>
class ArithmeticOperator : public MultiExpressionOperator<DerivedBy>
{
public:
    using Base = ArithmeticOperator;

    static constexpr const auto& ALLOWED_LITERALS = nosql::Operator::NUMBER_TYPES;

    using MultiExpressionOperator<DerivedBy>::MultiExpressionOperator;
};

/**
 * Operator::Accessor
 */
class Operator::Accessor : public ConcreteOperator<Operator::Accessor>
{
public:
    Accessor(const BsonView& value);

    BsonValue process(bsoncxx::document::view doc) override;

private:
    std::vector<std::string> m_fields;
};

/**
 * Operator::MultiAccessor
 */
class Operator::MultiAccessor : public ConcreteOperator<Operator::MultiAccessor>
{
public:
    MultiAccessor(const BsonView& value);

    BsonValue process(bsoncxx::document::view doc) override;

private:
    struct Field
    {
        Field(Field&&) = default;

        std::string               name;
        std::unique_ptr<Operator> sOp;
    };

    std::vector<Field> m_fields;
};

/**
 * Abs
 */
class Abs : public SingleExpressionOperator<Abs>
{
public:
    static constexpr const char* const NAME = "$abs";

    using Base::Base;

    BsonValue process(bsoncxx::document::view doc) override;
};

/**
 * Add
 */
class Add : public ArithmeticOperator<Add>
{
public:
    static constexpr const char* const NAME = "$add";

    Add(const BsonView& value)
        : ArithmeticOperator(value, 1)
    {
    }

    BsonValue process(bsoncxx::document::view doc) override;
};

/**
 * And
 */
class And : public MultiExpressionOperator<And>
{
public:
    static constexpr const char* const NAME = "$and";

    And(const BsonView& value)
        : Base(value, 2)
    {
    }

    BsonValue process(bsoncxx::document::view doc) override;
};

/**
 * Ceil
 */
class Ceil : public SingleExpressionOperator<Ceil>
{
public:
    static constexpr const char* const NAME = "$ceil";

    using Base::Base;

    BsonValue process(bsoncxx::document::view doc) override;
};

/**
 * Cmp
 */
class Cmp : public MultiExpressionOperator<Cmp>
{
public:
    static constexpr const char* const NAME = "$cmp";

    Cmp(const BsonView& value)
        : Base(value, 2, 2)
    {
    }

    BsonValue process(bsoncxx::document::view doc) override;
};

/**
 * Concat
 */
class Concat : public MultiExpressionOperator<Concat>
{
public:
    static constexpr const char* const NAME = "$cmp";

    Concat(const BsonView& value)
        : Base(value, 1)
    {
    }

    BsonValue process(bsoncxx::document::view doc) override;
};

/**
 * Cond
 */
class Cond : public ConcreteOperator<Cond>
{
public:
    static constexpr const char* const NAME = "$cond";

    Cond(const BsonView& value);

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

    Convert(const BsonView& value);

    BsonValue process(bsoncxx::document::view doc) override;

    static BsonValue to_bool(const BsonView& value, const BsonView& on_error = BsonView());
    static BsonValue to_date(const BsonView& value, const BsonView& on_error = BsonView());
    static BsonValue to_decimal(const BsonView& value, const BsonView& on_error = BsonView());
    static BsonValue to_double(const BsonView& value, const BsonView& on_error = BsonView());
    static BsonValue to_int32(const BsonView& value, const BsonView& on_error = BsonView());
    static BsonValue to_int64(const BsonView& value, const BsonView& on_error = BsonView());
    static BsonValue to_oid(const BsonView& value, const BsonView& on_error = BsonView());
    static BsonValue to_string(const BsonView& value, const BsonView& on_error = BsonView());

private:
    using Converter = BsonValue (*)(const BsonView& value, const BsonView& on_error);

    static Converter get_converter(bsoncxx::document::element e);
    static Converter get_converter(bsoncxx::type type);
    static Converter get_converter(std::string_view type);

    static BsonValue handle_decimal128_error(bsoncxx::decimal128 decimal,
                                             nobson::ConversionResult result,
                                             const BsonView& on_error);

    static BsonValue handle_default_case(bsoncxx::type from,
                                         bsoncxx::type to,
                                         const BsonView& on_error);

    std::unique_ptr<Operator> m_sInput;
    Converter                 m_to;
    BsonView                  m_on_error;
    BsonView                  m_on_null;
};

/**
 * Divide
 */
class Divide : public ArithmeticOperator<Divide>
{
public:
    static constexpr const char* const NAME = "$divide";

    Divide(const BsonView& value)
        : Base(value, 2, 2)
    {
    }

    BsonValue process(bsoncxx::document::view doc) override;
};

/**
 * Eq
 */
class Eq : public MultiExpressionOperator<Eq>
{
public:
    static constexpr const char* const NAME = "$eq";

    Eq(const BsonView& value)
        : Base(value, 2, 2)
    {
    }

    BsonValue process(bsoncxx::document::view doc) override;
};

/**
 * Exp
 */
class Exp : public SingleExpressionOperator<Exp>
{
public:
    static constexpr const char* const NAME = "$exp";

    using Base::Base;

    BsonValue process(bsoncxx::document::view doc) override;
};

/**
 * Floor
 */
class Floor : public SingleExpressionOperator<Floor>
{
public:
    static constexpr const char* const NAME = "$floor";

    using Base::Base;

    BsonValue process(bsoncxx::document::view doc) override;
};

/**
 * Gt
 */
class Gt : public MultiExpressionOperator<Gt>
{
public:
    static constexpr const char* const NAME = "$gt";

    Gt(const BsonView& value)
        : Base(value, 2, 2)
    {
    }

    BsonValue process(bsoncxx::document::view doc) override;
};

/**
 * Gte
 */
class Gte : public MultiExpressionOperator<Gt>
{
public:
    static constexpr const char* const NAME = "$gte";

    Gte(const BsonView& value)
        : Base(value, 2, 2)
    {
    }

    BsonValue process(bsoncxx::document::view doc) override;
};

/**
 * IfNull
 */
class IfNull : public MultiExpressionOperator<IfNull>
{
public:
    static constexpr const char* const NAME = "$ifNull";

    IfNull(const BsonView& value)
        : Base(value, 2, 2)
    {
    }

    BsonValue process(bsoncxx::document::view doc) override;
};

/**
 * Literal
 */
class Literal : public ConcreteOperator<Literal>
{
public:
    static constexpr const char* const NAME = "$literal";

    Literal(const BsonView& value);

    BsonValue process(bsoncxx::document::view doc) override;

private:
    BsonValue m_value;
};

/**
 * Ln
 */
class Ln : public SingleExpressionOperator<Exp>
{
public:
    static constexpr const char* const NAME = "$ln";

    using Base::Base;

    BsonValue process(bsoncxx::document::view doc) override;
};

/**
 * Log
 */
class Log : public MultiExpressionOperator<Log>
{
public:
    static constexpr const char* const NAME = "$log";

    Log(const BsonView& value)
        : Base(value, 2, 2)
    {
    }

    BsonValue process(bsoncxx::document::view doc) override;
};

/**
 * Log10
 */
class Log10 : public SingleExpressionOperator<Log10>
{
public:
    static constexpr const char* const NAME = "$log10";

    using Base::Base;

    BsonValue process(bsoncxx::document::view doc) override;
};

/**
 * Lt
 */
class Lt : public MultiExpressionOperator<Lt>
{
public:
    static constexpr const char* const NAME = "$lt";

    Lt(const BsonView& value)
        : Base(value, 2, 2)
    {
    }

    BsonValue process(bsoncxx::document::view doc) override;
};

/**
 * Lte
 */
class Lte : public MultiExpressionOperator<Lt>
{
public:
    static constexpr const char* const NAME = "$lte";

    Lte(const BsonView& value)
        : Base(value, 2, 2)
    {
    }

    BsonValue process(bsoncxx::document::view doc) override;
};

/**
 * Mod
 */
class Mod : public ArithmeticOperator<Mod>
{
public:
    static constexpr const char* const NAME = "$mod";

    Mod(const BsonView& value)
        : Base(value, 2, 2)
    {
    }

    BsonValue process(bsoncxx::document::view doc) override;
};

/**
 * Multiply
 */
class Multiply : public ArithmeticOperator<Multiply>
{
public:
    static constexpr const char* const NAME = "$multiply";

    Multiply(const BsonView& value)
        : Base(value, 1)
    {
    }

    BsonValue process(bsoncxx::document::view doc) override;
};

/**
 * Ne
 */
class Ne : public MultiExpressionOperator<Ne>
{
public:
    static constexpr const char* const NAME = "$ne";

    Ne(const BsonView& value)
        : Base(value, 2, 2)
    {
    }

    BsonValue process(bsoncxx::document::view doc) override;
};

/**
 * Not
 */
class Not : public ConcreteOperator<Not>
{
public:
    static constexpr const char* const NAME = "$not";

    Not(const BsonView& value);

    BsonValue process(bsoncxx::document::view doc) override;

private:
    std::unique_ptr<Operator> m_sOp;
};

/**
 * Or
 */
class Or : public MultiExpressionOperator<Or>
{
public:
    static constexpr const char* const NAME = "$or";

    Or(const BsonView& value)
        : Base(value, 2)
    {
    }

    BsonValue process(bsoncxx::document::view doc) override;
};

/**
 * Pow
 */
class Pow : public ArithmeticOperator<Pow>
{
public:
    static constexpr const char* const NAME = "$pow";

    Pow(const BsonView& value)
        : Base(value, 2, 2)
    {
    }

    BsonValue process(bsoncxx::document::view doc) override;
};

/**
 * Sqrt
 */
class Sqrt : public SingleExpressionOperator<Sqrt>
{
public:
    static constexpr const char* const NAME = "$sqrt";

    using Base::Base;

    BsonValue process(bsoncxx::document::view doc) override;
};

/**
 * Size
 */
class Size : public SingleExpressionOperator<Size>
{
public:
    static constexpr const char* const NAME = "$size";

    using Base::Base;

    BsonValue process(bsoncxx::document::view doc) override;
};

/**
 * Subtract
 */
class Subtract : public ArithmeticOperator<Subtract>
{
public:
    static constexpr const char* const NAME = "$subtract";

    Subtract(const BsonView& value)
        : Base(value, 2, 2)
    {
    }

    BsonValue process(bsoncxx::document::view doc) override;
};

/**
 * ToBool
 */
class ToBool : public SingleExpressionOperator<ToBool>
{
public:
    static constexpr const char* const NAME = "$toBool";

    using Base::Base;

    BsonValue process(bsoncxx::document::view doc) override;
};

/**
 * ToDate
 */
class ToDate : public SingleExpressionOperator<ToDate>
{
public:
    static constexpr const char* const NAME = "$toDate";

    using Base::Base;

    BsonValue process(bsoncxx::document::view doc) override;
};

/**
 * ToDecimal
 */
class ToDecimal : public SingleExpressionOperator<ToDecimal>
{
public:
    static constexpr const char* const NAME = "$toDecimal";

    using Base::Base;

    BsonValue process(bsoncxx::document::view doc) override;
};

/**
 * ToDouble
 */
class ToDouble : public SingleExpressionOperator<ToDouble>
{
public:
    static constexpr const char* const NAME = "$toDouble";

    using Base::Base;

    BsonValue process(bsoncxx::document::view doc) override;
};

/**
 * ToInt
 */
class ToInt : public SingleExpressionOperator<ToInt>
{
public:
    static constexpr const char* const NAME = "$toInt";

    using Base::Base;

    BsonValue process(bsoncxx::document::view doc) override;
};

/**
 * ToLong
 */
class ToLong : public SingleExpressionOperator<ToLong>
{
public:
    static constexpr const char* const NAME = "$toLong";

    using Base::Base;

    BsonValue process(bsoncxx::document::view doc) override;
};

/**
 * ToObjectId
 */
class ToObjectId : public SingleExpressionOperator<ToObjectId>
{
public:
    static constexpr const char* const NAME = "$toObjectId";

    using Base::Base;

    BsonValue process(bsoncxx::document::view doc) override;
};

/**
 * ToString
 */
class ToString : public SingleExpressionOperator<ToString>
{
public:
    static constexpr const char* const NAME = "$toString";

    using Base::Base;

    BsonValue process(bsoncxx::document::view doc) override;
};

}
}
