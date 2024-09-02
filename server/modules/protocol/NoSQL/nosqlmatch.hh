/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
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

#include "nosqlprotocol.hh"
#include <bsoncxx/types/bson_value/view.hpp>
#include "nosqlfieldpath.hh"

namespace nosql
{

class Match
{
public:
    class Condition;
    using SCondition = std::unique_ptr<Condition>;
    using SConditions = std::vector<SCondition>;

    class Evaluator;
    using SEvaluator = std::unique_ptr<Evaluator>;

    Match(const Match&) = delete;
    Match& operator=(const Match&) = delete;

    Match(bsoncxx::document::view match);

    std::string sql() const;

    bool matches(bsoncxx::document::view doc) const;

    class Condition
    {
    public:
        using BsonView = bsoncxx::types::bson_value::view;
        using Creator = std::unique_ptr<Condition>(*)(const BsonView& view);

        virtual ~Condition() = default;

        virtual std::string generate_sql() const = 0;

        virtual bool matches(bsoncxx::document::view doc) const = 0;

        static SCondition create(std::string_view name, const BsonView& view);
        static SCondition create(const bsoncxx::document::element& element)
        {
            return create(element.key(), element.get_value());
        }

        static SCondition create(bsoncxx::document::view doc);

    protected:
        SConditions logical_condition(const BsonView& view, const char* zOp);
    };

    class Evaluator
    {
    public:
        using BsonView = bsoncxx::types::bson_value::view;
        using Creator = std::unique_ptr<Evaluator>(*)(const FieldPath* pField_path, const BsonView& view);

        virtual ~Evaluator() = default;

        virtual bool matches(bsoncxx::document::view doc) const final;

        virtual bool matches(const bsoncxx::types::bson_value::view& view) const = 0;

        static SEvaluator create(const FieldPath* pField_path,
                                 std::string_view name,
                                 const BsonView& view);
        static SEvaluator create(const FieldPath* pField_path, const bsoncxx::document::element& element)
        {
            return create(pField_path, element.key(), element.get_value());
        }
        static SEvaluator create(const FieldPath* pField_path, bsoncxx::document::view doc);
        static SEvaluator create(const FieldPath* pField_path,
                                 const bsoncxx::types::bson_value::view& view);

    protected:
        Evaluator(const FieldPath* pField_path)
            : m_field_path(*pField_path)
        {
        }

        const FieldPath& m_field_path;
    };

private:
    static SConditions create(bsoncxx::document::view doc);

    mutable std::string m_sql;
    SConditions         m_conditions;
};

namespace condition
{

template<class DerivedBy, class DerivedFrom = Match::Condition>
class ConcreteCondition : public DerivedFrom
{
public:
    using Base = ConcreteCondition<DerivedBy, DerivedFrom>;

    static std::unique_ptr<DerivedFrom> create(const typename DerivedFrom::BsonView& view)
    {
        return std::make_unique<DerivedBy>(view);
    }
};

template<class DerivedBy, class DerivedFrom = Match::Condition>
class LogicalCondition : public ConcreteCondition<DerivedBy, DerivedFrom>
{
public:
    std::string generate_sql() const override final
    {
        std::string sql;

        for (auto& sCondition : m_conditions)
        {
            std::string condition = sCondition->generate_sql();

            if (condition.empty())
            {
                // TODO: This is like it was done in the old code, but it's not clear why.
                sql.clear();
                break;
            }
            else
            {
                add_sql(sql, condition);
            }
        }

        if (!sql.empty())
        {
            sql = "(" + sql + ")";
        }

        return sql;
    }

protected:
    virtual void add_sql(std::string& sql, const std::string& condition) const = 0;

    using Base = LogicalCondition<DerivedBy, DerivedFrom>;

    LogicalCondition(const typename DerivedFrom::BsonView& view, const char* zOp)
        : m_conditions(DerivedFrom::logical_condition(view, zOp))
    {
    }

    LogicalCondition(Match::SConditions&& conditions)
        : m_conditions(std::move(conditions))
    {
    }

    Match::SConditions m_conditions;
};

/**
 * AlwaysFalse
 */
class AlwaysFalse final : public ConcreteCondition<AlwaysFalse>
{
public:
    static constexpr const char* const NAME = "$alwaysFalse";

    AlwaysFalse(const BsonView& view);

    std::string generate_sql() const override;

    bool matches(bsoncxx::document::view doc) const override;
};

/**
 * AlwaysTrue
 */
class AlwaysTrue final : public ConcreteCondition<AlwaysTrue>
{
public:
    static constexpr const char* const NAME = "$alwaysTrue";

    AlwaysTrue();
    AlwaysTrue(const BsonView& view);

    std::string generate_sql() const override;

    bool matches(bsoncxx::document::view doc) const override;
};

/**
 * And
 */
class And final : public LogicalCondition<And>
{
public:
    static constexpr const char* const NAME = "$and";

    And(const BsonView& view)
        : LogicalCondition(view, NAME)
    {
    }

    And(Match::SConditions&& conditions)
        : LogicalCondition(std::move(conditions))
    {
    }

    bool matches(bsoncxx::document::view doc) const override;

private:
    void add_sql(std::string& sql, const std::string& condition) const override;
};

/**
 * Or
 */
class Or final : public LogicalCondition<Or>
{
public:
    static constexpr const char* const NAME = "$or";

    Or(const BsonView& view)
        : LogicalCondition(view, NAME)
    {
    }

    bool matches(bsoncxx::document::view doc) const override;

private:
    void add_sql(std::string& sql, const std::string& condition) const override;
};

/**
 * Nor
 */
class Nor final : public LogicalCondition<Nor>
{
public:
    static constexpr const char* const NAME = "$nor";

    Nor(const BsonView& view)
        : LogicalCondition(view, NAME)
    {
    }

    bool matches(bsoncxx::document::view doc) const override;

private:
    void add_sql(std::string& sql, const std::string& condition) const override;
};

}

namespace evaluator
{

template<class DerivedBy>
class ConcreteEvaluator : public Match::Evaluator
{
public:
    using Match::Evaluator::Evaluator;
    using Base = ConcreteEvaluator<DerivedBy>;

    static std::unique_ptr<Match::Evaluator> create(const FieldPath* pField_path,
                                                    const Match::Evaluator::BsonView& view)
    {
        return std::make_unique<DerivedBy>(pField_path, view);
    }
};

/**
 * Eq
 */
class Eq : public ConcreteEvaluator<Eq>
{
public:
    static constexpr const char* const NAME = "$eq";

    Eq(const FieldPath* pField_path, const BsonView& view)
        : Base(pField_path)
        , m_view(view)
    {
    }

    bool matches(const bsoncxx::types::bson_value::view& view) const override;

private:
    bsoncxx::types::bson_value::view m_view;
};

/**
 * Type
 */
class Type : public ConcreteEvaluator<Type>
{
public:
    static constexpr const char* const NAME = "$type";

    Type(const FieldPath* pField_path, const BsonView& view);

    bool matches(const bsoncxx::types::bson_value::view& view) const override;

private:
    static std::vector<bsoncxx::type> get_types(const bsoncxx::types::bson_value::view& view);
    static void get_types(std::vector<bsoncxx::type>& types, const bsoncxx::types::bson_value::view& view);

    std::vector<bsoncxx::type> m_types;
};


}

}
