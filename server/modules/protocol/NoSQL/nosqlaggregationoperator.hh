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
    using BsonValue = bsoncxx::types::bson_value::value;
    using BsonView = bsoncxx::types::bson_value::view;
    using Creator = std::unique_ptr<Operator>(*)(const BsonView& value);
    class Accessor;
    class Literal;

    virtual ~Operator();

    static void unsupported(string_view key);

    static std::unique_ptr<Operator> create(const BsonView& value);

    bool ready() const
    {
        return m_ready;
    }

    virtual const BsonValue& process(bsoncxx::document::view doc) = 0;

    const BsonValue& value() const
    {
        return m_value;
    }

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

    static std::unique_ptr<Operator> create(const BsonView& value)
    {
        return std::make_unique<Derived>(value);
    }
};

template<class Derived>
class SingleExpressionOperator : public ConcreteOperator<Derived>
{
public:
    using Base = SingleExpressionOperator;

    SingleExpressionOperator(const Operator::BsonView& value)
        : m_sOp(Operator::create(value))
    {
    }

protected:
    std::unique_ptr<Operator> m_sOp;
};

template<class Derived>
class MultiExpressionOperator : public ConcreteOperator<Derived>
{
public:
    using Base = MultiExpressionOperator;

    MultiExpressionOperator()
    {
    }

protected:
    std::vector<std::unique_ptr<Operator>> m_ops;
};

/**
 * Operator::Accessor
 */
class Operator::Accessor : public ConcreteOperator<Operator::Accessor>
{
public:
    Accessor(const BsonView& value);

    const BsonValue& process(bsoncxx::document::view doc) override;

private:
    std::vector<std::string> m_fields;
};

/**
 * Operator::Literal
 */
class Operator::Literal : public ConcreteOperator<Operator::Literal>
{
public:
    Literal(const BsonView& value);

    const BsonValue& process(bsoncxx::document::view doc) override;
};

/**
 * Avg
 */
class Avg : public SingleExpressionOperator<Avg>
{
public:
    static constexpr const char* const NAME = "$avg";

    using Base::Base;

    const BsonValue& process(bsoncxx::document::view doc) override;

private:
    int32_t m_count {0};
};

/**

 * Cond
 */
class Cond : public ConcreteOperator<Cond>
{
public:
    static constexpr const char* const NAME = "$cond";

    Cond(const BsonView& value);

    const BsonValue& process(bsoncxx::document::view doc) override;

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

    const BsonValue& process(bsoncxx::document::view doc) override;

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
class Divide : public MultiExpressionOperator<Divide>
{
public:
    static constexpr const char* const NAME = "$divide";

    Divide(const BsonView& value);

    const BsonValue& process(bsoncxx::document::view doc) override;
};

/**
 * Eq
 */
class Eq : public MultiExpressionOperator<Eq>
{
public:
    static constexpr const char* const NAME = "$eq";

    Eq(const BsonView& value);

    const BsonValue& process(bsoncxx::document::view doc) override;
};

/**
 * First
 */
class First : public SingleExpressionOperator<First>
{
public:
    static constexpr const char* const NAME = "$first";

    using Base::Base;

    const BsonValue& process(bsoncxx::document::view doc) override;
};

/**
 * Last
 */
class Last : public SingleExpressionOperator<Last>
{
public:
    static constexpr const char* const NAME = "$last";

    using Base::Base;

    const BsonValue& process(bsoncxx::document::view doc) override;
};

/**
 * Max
 */
class Max : public SingleExpressionOperator<Max>
{
public:
    static constexpr const char* const NAME = "$max";

    using Base::Base;

    const BsonValue& process(bsoncxx::document::view doc) override;

private:
    bool m_first { true };
};

/**
 * Min
 */
class Min : public SingleExpressionOperator<Min>
{
public:
    static constexpr const char* const NAME = "$min";

    using Base::Base;

    const BsonValue& process(bsoncxx::document::view doc) override;

private:
    bool m_first { true };
};

/**
 * Multiply
 */
class Multiply : public MultiExpressionOperator<Multiply>
{
public:
    static constexpr const char* const NAME = "$multiply";

    Multiply(const BsonView& value);

    const BsonValue& process(bsoncxx::document::view doc) override;
};

/**
 * Ne
 */
class Ne : public MultiExpressionOperator<Ne>
{
public:
    static constexpr const char* const NAME = "$ne";

    Ne(const BsonView& value);

    const BsonValue& process(bsoncxx::document::view doc) override;
};

/**
 * Subtract
 */
class Subtract : public SingleExpressionOperator<Subtract>
{
public:
    static constexpr const char* const NAME = "$subtract";

    using Base::Base;

    const BsonValue& process(bsoncxx::document::view doc) override;
};

/**
 * Sum
 */
class Sum : public SingleExpressionOperator<Sum>
{
public:
    static constexpr const char* const NAME = "$sum";

    using Base::Base;

    const BsonValue& process(bsoncxx::document::view doc) override;
};

/**
 * ToBool
 */
class ToBool : public SingleExpressionOperator<ToBool>
{
public:
    static constexpr const char* const NAME = "$toBool";

    using Base::Base;

    const BsonValue& process(bsoncxx::document::view doc) override;
};

/**
 * ToDate
 */
class ToDate : public SingleExpressionOperator<ToDate>
{
public:
    static constexpr const char* const NAME = "$toDate";

    using Base::Base;

    const BsonValue& process(bsoncxx::document::view doc) override;
};

/**
 * ToDecimal
 */
class ToDecimal : public SingleExpressionOperator<ToDecimal>
{
public:
    static constexpr const char* const NAME = "$toDecimal";

    using Base::Base;

    const BsonValue& process(bsoncxx::document::view doc) override;
};

/**
 * ToDouble
 */
class ToDouble : public SingleExpressionOperator<ToDouble>
{
public:
    static constexpr const char* const NAME = "$toDouble";

    using Base::Base;

    const BsonValue& process(bsoncxx::document::view doc) override;
};

/**
 * ToInt
 */
class ToInt : public SingleExpressionOperator<ToInt>
{
public:
    static constexpr const char* const NAME = "$toInt";

    using Base::Base;

    const BsonValue& process(bsoncxx::document::view doc) override;
};

/**
 * ToLong
 */
class ToLong : public SingleExpressionOperator<ToLong>
{
public:
    static constexpr const char* const NAME = "$toLong";

    using Base::Base;

    const BsonValue& process(bsoncxx::document::view doc) override;
};

/**
 * ToObjectId
 */
class ToObjectId : public SingleExpressionOperator<ToObjectId>
{
public:
    static constexpr const char* const NAME = "$toObjectId";

    using Base::Base;

    const BsonValue& process(bsoncxx::document::view doc) override;
};

/**
 * ToString
 */
class ToString : public SingleExpressionOperator<ToString>
{
public:
    static constexpr const char* const NAME = "$toString";

    using Base::Base;

    const BsonValue& process(bsoncxx::document::view doc) override;
};

}
}
