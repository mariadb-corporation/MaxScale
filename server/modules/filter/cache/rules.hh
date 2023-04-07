#pragma once
/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-03-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/ccdefs.hh>
#include <stdbool.h>
#include <memory>
#include <vector>
#include <maxbase/jansson.hh>
#include <maxscale/buffer.hh>
#include <maxscale/session.hh>
#include <maxscale/pcre2.hh>
#include "cacheconfig.hh"


class CacheRuleConcrete;
class CacheRuleUser;

class CacheRule
{
public:
    enum class Attribute
    {
        COLUMN,
        DATABASE,
        QUERY,
        TABLE,
        USER,
    };

    static const char* to_string(Attribute attribute);
    static bool from_string(const char* z, Attribute* pAttribute);

    enum class Op
    {
        EQ,
        NEQ,
        LIKE,
        UNLIKE
    };

    static const char* to_string(Op op);
    static bool from_string(const char* z, Op* pOp);

    virtual ~CacheRule();

    virtual const CacheConfig& config() const = 0;
    virtual Attribute attribute() const = 0;
    virtual Op op() const = 0;
    virtual std::string value() const = 0;
    virtual uint32_t debug() const = 0;

    virtual bool compare(const std::string_view& value) const = 0;
    virtual bool compare_n(const char* pValue, size_t length) const = 0;

    virtual bool eq(const CacheRule& other) const = 0;

    virtual bool eq(const CacheRuleConcrete& other) const;
    virtual bool eq(const CacheRuleUser& other) const;
};

inline bool operator == (const CacheRule& lhs, const CacheRule& rhs)
{
    return lhs.eq(rhs);
}

class CacheRuleConcrete : public CacheRule
{
public:
    const CacheConfig& config() const override final
    {
        return m_config;
    }

    Attribute attribute() const override final
    {
        return m_attribute;
    }

    Op op() const override final
    {
        return m_op;
    }

    std::string value() const override final
    {
        return m_value;
    }

    uint32_t debug() const override final
    {
        return m_config.debug;
    }

    bool compare(const std::string_view& value) const override final;

    bool eq(const CacheRule& other) const override final;
    bool eq(const CacheRuleConcrete& other) const override final;

protected:
    CacheRuleConcrete(const CacheConfig* pConfig,
                      Attribute attribute,
                      Op op,
                      std::string value)
        : m_config(*pConfig)
        , m_attribute(attribute)
        , m_op(op)
        , m_value(std::move(value))
    {
    }

    const CacheConfig& m_config;    // The cache config.
    Attribute          m_attribute; // What attribute is evaluated.
    Op                 m_op;        // What operator is used.
    std::string        m_value;     // The value from the rule file.
};

class CacheRuleValue : public CacheRuleConcrete
{
public:
    bool matches(const mxs::Parser& parser, const char* zDefault_db, const GWBUF* pQuery) const;

protected:
    virtual bool matches_column(const mxs::Parser& parser, const char* zDefault_db, const GWBUF* pQuery) const;
    virtual bool matches_table(const mxs::Parser& parser, const char* zDefault_db,
                               const GWBUF* pQuery) const;
    bool matches_database(const mxs::Parser& parser, const char* zDefault_db, const GWBUF* pQuery) const;
    bool matches_query(const char* zDefault_db, const GWBUF* pQuery) const;

protected:
    CacheRuleValue(const CacheConfig* pConfig,
                   Attribute attribute,
                   Op op,
                   std::string value)
        : CacheRuleConcrete(pConfig, attribute, op, value)
    {
    }
};

class CacheRuleSimple : public CacheRuleValue
{
public:
    static bool compare_n(const std::string& lhs,
                          Op op,
                          const char* pValue, size_t length);

protected:
    CacheRuleSimple(const CacheConfig* pConfig,
                    Attribute attribute,
                    Op op,
                    std::string value)
        : CacheRuleValue(pConfig, attribute, op, value)
    {
        mxb_assert(op == Op::EQ || op == Op::NEQ);
    }

    bool compare_n(const char* pValue, size_t length) const override final;
};

class CacheRuleCTD final : public CacheRuleSimple
{
public:
    static CacheRuleCTD* create(const CacheConfig* pConfig,
                                Attribute attribute,
                                Op op,
                                const char* zValue);

protected:
    bool matches_column(const mxs::Parser& parser,
                        const char* zDefault_db,
                        const GWBUF* pQuery) const override;
    bool matches_table(const mxs::Parser& parser,
                       const char* zDefault_db,
                       const GWBUF* pQuery) const override;

    std::string m_column;
    std::string m_table;
    std::string m_database;

private:
    CacheRuleCTD(const CacheConfig* pConfig,
                 Attribute attribute,
                 Op op,
                 std::string value)
        : CacheRuleSimple(pConfig, attribute, op, value)
    {
    }
};

class CacheRuleQuery final : public CacheRuleSimple
{
public:
    static CacheRuleQuery* create(const CacheConfig* pConfig, // The cache config.
                                  Attribute attribute,        // What attribute is evaluated.
                                  Op op,                      // What operator is used.
                                  const char* zValue);        // The value from the rule file.

private:
    CacheRuleQuery(const CacheConfig* pConfig,
                   Attribute attribute,
                   Op op,
                   std::string value)
        : CacheRuleSimple(pConfig, attribute, op, value)
    {
    }
};

class CacheRuleRegex final : public CacheRuleValue
{
public:
    ~CacheRuleRegex();

    bool compare_n(const char* pValue, size_t length) const override final;

    static CacheRuleRegex* create(const CacheConfig* pConfig, // The cache config.
                                  Attribute attribute,        // What attribute is evaluated.
                                  Op op,                      // What operator is used.
                                  const char* zValue);        // The value from the rule file.

protected:
    bool matches_column(const mxs::Parser& parser,
                        const char* zDefault_db,
                        const GWBUF* pQuery) const override;
    bool matches_table(const mxs::Parser& parser,
                       const char* zDefault_db,
                       const GWBUF* pQuery) const override;

private:
    CacheRuleRegex(const CacheConfig* pConfig,
                   Attribute attribute,
                   Op op,
                   std::string value)
        : CacheRuleValue(pConfig, attribute, op, value)
    {
        mxb_assert(op == Op::LIKE || op == Op::UNLIKE);
    }

    pcre2_code* m_pCode;
};

class CacheRuleUser final : public CacheRule
{
public:
    static CacheRuleUser* create(const CacheConfig* pConfig, // The cache config.
                                 Attribute attribute,        // What attribute is evaluated.
                                 Op op,                      // What operator is used.
                                 const char* zValue);        // The value from the rule file.

    const CacheConfig& config() const override
    {
        return m_sDelegate->config();
    }

    Attribute attribute() const override
    {
        return m_sDelegate->attribute();
    }

    Op op() const override
    {
        return m_sDelegate->op();
    }

    std::string value() const override
    {
        return m_sDelegate->value();
    }

    uint32_t debug() const override
    {
        return m_sDelegate->debug();
    }

    bool matches_user(const char* zAccount) const;

    bool compare(const std::string_view& value) const override;
    bool compare_n(const char* pValue, size_t length) const override;

    bool eq(const CacheRule& other) const override;
    bool eq(const CacheRuleUser& other) const override;

private:
    CacheRuleUser(std::unique_ptr<CacheRule> sDelegate)
        : m_sDelegate(std::move(sDelegate))
    {
    }

    std::unique_ptr<CacheRule> m_sDelegate;
};


class CacheRules
{
public:
    class Tester;

    using SCacheRules = std::shared_ptr<CacheRules>;
    using S = SCacheRules;
    using Vector = std::vector<S>;
    using SVector = std::shared_ptr<Vector>;

    CacheRules(const CacheRules&) = delete;
    CacheRules& operator=(const CacheRules&) = delete;

    ~CacheRules();

    /**
     * Get rules.
     *
     * @param path  The path to the rules file. If empty, a default rule will be returned.
     *
     * @return The corresponding rules, or null if the rules file could not be opened or parsed.
     */
    static SVector get(const CacheConfig* pConfig, const std::string& path);

    /**
     * Creates an empty rules object.
     *
     * @param pConfig The cache config.
     *
     * @return An empty rules object, or NULL in case of error.
     */
    static std::unique_ptr<CacheRules> create(const CacheConfig* pConfig);

    /**
     * Parses the caching rules from a string.
     *
     * @param pConfig The cache config.
     * @param zJson   Null-terminate string containing JSON.
     *
     * @return Rules vector if the rules could be parsed, null otherwise.
     */
    static SVector parse(const CacheConfig* pConfig, const char* zJson);

    /**
     * Loads the caching rules from a file.
     *
     * @param pConfig The cache config.
     * @param path    The path of the file containing the rules.
     *
     * @return Rules vector if the rules could be parsed, null otherwise.
     */
    static SVector load(const CacheConfig* pConfig, const char* zPath);
    static SVector load(const CacheConfig* pConfig, const std::string& path)
    {
        return load(pConfig, path.c_str());
    }

    /**
     * Returns the json rules object.
     *
     * NOTE: The object remains valid only as long as the CacheRules
     *       object is valid.
     *
     * @return The rules object.
     */
    const json_t* json() const;

    /**
     * Returns boolean indicating whether the result of the query should be stored.
     *
     * @param parser      The parser to use.
     * @param zdefault_db The current default database, NULL if there is none.
     * @param pquery      The query, expected to contain a COM_QUERY.
     *
     * @return True, if the results should be stored.
     */
    bool should_store(const mxs::Parser& parser, const char* zDefault_db, const GWBUF* pQuery) const;

    /**
     * Returns boolean indicating whether the cache should be used, that is consulted.
     *
     * @param psession  The current session.
     *
     * @return True, if the cache should be used.
     */
    bool should_use(const MXS_SESSION* pSession) const;

    /**
     * Compare rules for equality.
     *
     * @param other  The rules to compare to.
     *
     * @return True, if this and @c other are equivalent.
     */
    bool eq(const CacheRules& other) const;

    /**
     * Compare vector of rules for equality.
     *
     * @param lhs One vector of rules.
     * @param rhs Another vector of rules.
     *
     * @return True, if @c lhs and @c rhs are equal.
     */
    static bool eq(const Vector& lhs, const Vector& rhs);

    /**
     * Compare vector of rules for equality.
     *
     * @param sLhs Pointer to vector of rules.
     * @param sRhs Pointer to another vector of rules.
     *
     * @return True, if the vectors of rules pointed to by @c sLhs and @c sRhs are equal.
     */
    static bool eq(const SVector& sLhs, const SVector& sRhs);

private:
    friend class Tester;

    CacheRules(const CacheConfig* pConfig);

    static SVector create_all_from_json(const CacheConfig* pConfig, json_t* pRoot);
    static CacheRules* create_one_from_json(const CacheConfig* pConfig, json_t* pRoot);

    bool parse_json(json_t* pRoot);

    using ElementParser = bool (CacheRules::*)(json_t* pObject, size_t index);

    bool parse_array(json_t* pRoot, const char* zName, ElementParser);

    bool parse_store_element(json_t* pObject, size_t index);
    bool parse_use_element(json_t* pObject, size_t index);

    using Attributes = std::set<CacheRule::Attribute>;

    CacheRule* parse_element(json_t* pObject,
                             const char* zName,
                             size_t index,
                             const Attributes& valid_attributes);

    static bool get_attribute(const Attributes& valid_attributes,
                              const char* z,
                              CacheRule::Attribute* pAttribute);

    static CacheRule* create_simple_rule(const CacheConfig* pConfig,
                                         CacheRule::Attribute attribute,
                                         CacheRule::Op op,
                                         const char* zValue);

    static CacheRule* create_rule(const CacheConfig* pConfig,
                                  CacheRule::Attribute attribute,
                                  CacheRule::Op op,
                                  const char* zValue);

    static Attributes s_store_attributes;
    static Attributes s_use_attributes;

    using SCacheRuleValue = std::unique_ptr<CacheRuleValue>;
    using SCacheRuleUser = std::unique_ptr<CacheRuleUser>;

    using SCacheRuleValueVector = std::vector<SCacheRuleValue>;
    using SCacheRuleUserVector = std::vector<SCacheRuleUser>;

    const CacheConfig&    m_config;            // The cache config.
    json_t*               m_pRoot { nullptr }; // The JSON root object.
    SCacheRuleValueVector m_store_rules;       // The rules for when to store data to the cache.
    SCacheRuleUserVector  m_use_rules;         // The rules for when to use data from the cache.
};

inline bool operator == (const CacheRules& lhs, const CacheRules& rhs)
{
    return lhs.eq(rhs);
}
