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
#include <maxbase/json.hh>
#include <bsoncxx/types/value.hpp>
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

    virtual ~Operator();

    static void unsupported(string_view key);

    static std::unique_ptr<Operator> create(bsoncxx::types::value value);

    static std::unique_ptr<Operator> create_expression_operator(bsoncxx::types::value value);

    bool ready() const
    {
        return m_ready;
    }

    virtual bsoncxx::types::value process(bsoncxx::document::view doc) = 0;

    bsoncxx::types::value value() const
    {
        return m_value;
    }

    using Number = std::variant<int32_t, int64_t, double>;

    static Number mul(const Number& lhs, const Number& rhs);

protected:
    Operator()
    {
    }

    Operator(const bsoncxx::types::value& value)
        : m_value(value)
    {
    }

    void set_ready()
    {
        m_ready = true;
    }

    bsoncxx::types::value m_value;

private:
    bool m_ready { false };
};

template<class Derived>
class ConcreteOperator : public Operator
{
public:
    using Operator::Operator;

    static std::unique_ptr<Operator> create(bsoncxx::types::value value)
    {
        return std::make_unique<Derived>(value);
    }
};

/**
 * Accessor
 */
class Accessor : public ConcreteOperator<Accessor>
{
public:
    Accessor(bsoncxx::types::value value);

    bsoncxx::types::value process(bsoncxx::document::view doc) override;

private:
    std::vector<std::string> m_fields;
};

/**
 * Literal
 */
class Literal : public ConcreteOperator<Literal>
{
public:
    Literal(bsoncxx::types::value value);

    bsoncxx::types::value process(bsoncxx::document::view doc) override;
};

/**
 * Cond
 */
class Cond : public ConcreteOperator<Cond>
{
public:
    static constexpr const char* const NAME = "$cond";

    Cond(bsoncxx::types::value value);

    bsoncxx::types::value process(bsoncxx::document::view doc) override;

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

    Convert(bsoncxx::types::value value);

    bsoncxx::types::value process(bsoncxx::document::view doc) override;

    static bsoncxx::types::value to_bool(ArrayBuilder& builder,
                                         bsoncxx::types::value value,
                                         bsoncxx::types::value on_error = bsoncxx::types::value());
    static bsoncxx::types::value to_date(ArrayBuilder& builder,
                                         bsoncxx::types::value value,
                                         bsoncxx::types::value on_error = bsoncxx::types::value());
    static bsoncxx::types::value to_decimal(ArrayBuilder& builder,
                                            bsoncxx::types::value value,
                                            bsoncxx::types::value on_error = bsoncxx::types::value());
    static bsoncxx::types::value to_double(ArrayBuilder& builder,
                                           bsoncxx::types::value value,
                                           bsoncxx::types::value on_error = bsoncxx::types::value());
    static bsoncxx::types::value to_int32(ArrayBuilder& builder,
                                          bsoncxx::types::value value,
                                          bsoncxx::types::value on_error = bsoncxx::types::value());
    static bsoncxx::types::value to_int64(ArrayBuilder& builder,
                                          bsoncxx::types::value value,
                                          bsoncxx::types::value on_error = bsoncxx::types::value());
    static bsoncxx::types::value to_oid(ArrayBuilder& builder,
                                        bsoncxx::types::value value,
                                        bsoncxx::types::value on_error = bsoncxx::types::value());
    static bsoncxx::types::value to_string(ArrayBuilder& builder,
                                           bsoncxx::types::value value,
                                           bsoncxx::types::value on_error = bsoncxx::types::value());

private:
    using Converter = bsoncxx::types::value (*)(ArrayBuilder& builder,
                                                bsoncxx::types::value value,
                                                bsoncxx::types::value on_error);

    static Converter get_converter(bsoncxx::document::element e);
    static Converter get_converter(bsoncxx::type type);
    static Converter get_converter(std::string_view type);

    static void handle_decimal128_error(ArrayBuilder& builder,
                                        bsoncxx::decimal128 decimal,
                                        nobson::ConversionResult result,
                                        bsoncxx::types::value on_error);

    static void handle_default_case(ArrayBuilder& builder,
                                    bsoncxx::type from,
                                    bsoncxx::type to,
                                    bsoncxx::types::value on_error);

    std::unique_ptr<Operator> m_sInput;
    Converter                 m_to;
    bsoncxx::types::value     m_on_error;
    bsoncxx::types::value     m_on_null;
    ArrayBuilder              m_builder;
};

/**
 * Divide
 */
class Divide : public ConcreteOperator<Divide>
{
public:
    static constexpr const char* const NAME = "$divide";

    Divide(bsoncxx::types::value value);

    bsoncxx::types::value process(bsoncxx::document::view doc) override;

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

    First(bsoncxx::types::value value);

    bsoncxx::types::value process(bsoncxx::document::view doc) override;

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

    Max(bsoncxx::types::value value);

    bsoncxx::types::value process(bsoncxx::document::view doc) override;

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

    Multiply(bsoncxx::types::value value);

    bsoncxx::types::value process(bsoncxx::document::view doc) override;

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

    Ne(bsoncxx::types::value value);

    bsoncxx::types::value process(bsoncxx::document::view doc) override;

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

    Sum(bsoncxx::types::value value);

    bsoncxx::types::value process(bsoncxx::document::view doc) override;

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

    ToBool(bsoncxx::types::value value);

    bsoncxx::types::value process(bsoncxx::document::view doc) override;

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

    ToDate(bsoncxx::types::value value);

    bsoncxx::types::value process(bsoncxx::document::view doc) override;

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

    ToDecimal(bsoncxx::types::value value);

    bsoncxx::types::value process(bsoncxx::document::view doc) override;

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

    ToDouble(bsoncxx::types::value value);

    bsoncxx::types::value process(bsoncxx::document::view doc) override;

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

    ToInt(bsoncxx::types::value value);

    bsoncxx::types::value process(bsoncxx::document::view doc) override;

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

    ToLong(bsoncxx::types::value value);

    bsoncxx::types::value process(bsoncxx::document::view doc) override;

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

    ToObjectId(bsoncxx::types::value value);

    bsoncxx::types::value process(bsoncxx::document::view doc) override;

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

    ToString(bsoncxx::types::value value);

    bsoncxx::types::value process(bsoncxx::document::view doc) override;

private:
    std::unique_ptr<Operator> m_sOp;
    ArrayBuilder              m_builder;
};

}
}
