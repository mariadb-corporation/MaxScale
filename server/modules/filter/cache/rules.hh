#pragma once
/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-12-27
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

    virtual Attribute attribute() const = 0;
    virtual Op op() const = 0;
    virtual std::string value() const = 0;
    virtual uint32_t debug() const = 0;

    virtual bool compare(const std::string_view& value) const = 0;
    virtual bool compare_n(const char* value, size_t length) const = 0;
};

class CacheRuleConcrete : public CacheRule
{
public:
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
        return m_debug;
    }

    bool compare(const std::string_view& value) const override final;

protected:
    CacheRuleConcrete(Attribute attribute, // What attribute is evalued.
                      Op op,               // What operator is used.
                      std::string value,                // The value from the rule file.
                      uint32_t debug)                   // Debug bits
        : m_attribute(attribute)
        , m_op(op)
        , m_value(std::move(value))
        , m_debug(debug)
    {
    }

    Attribute m_attribute;   // What attribute is evalued.
    Op        m_op;          // What operator is used.
    std::string            m_value;       // The value from the rule file.
    uint32_t               m_debug;       // The debug bits.

};

class CacheRuleValue : public CacheRuleConcrete
{
public:
    bool matches(const char* default_db, const GWBUF* query) const;

protected:
    virtual bool matches_column(const char* default_db, const GWBUF* query) const;
    virtual bool matches_table(const char* default_db, const GWBUF* query) const;
    bool matches_database(const char* default_db, const GWBUF* query) const;
    bool matches_query(const char* default_db, const GWBUF* query) const;

protected:
    CacheRuleValue(Attribute attribute, // What attribute is evalued.
                   Op op,               // What operator is used.
                   std::string value,                // The value from the rule file.
                   uint32_t debug)                   // Debug bits
        : CacheRuleConcrete(attribute, op, value, debug)
    {
    }
};

class CacheRuleSimple : public CacheRuleValue
{
public:
    static bool compare_n(const std::string& lhs,
                          Op op,
                          const char* value, size_t length);

protected:
    CacheRuleSimple(Attribute attribute, // What attribute is evalued.
                    Op op,               // What operator is used.
                    std::string value,                // The value from the rule file.
                    uint32_t debug)                   // Debug bits
        : CacheRuleValue(attribute, op, value, debug)
    {
        mxb_assert(op == Op::EQ || op == Op::NEQ);
    }

    bool compare_n(const char* value, size_t length) const override final;
};

class CacheRuleCTD final : public CacheRuleSimple
{
public:
    static CacheRuleCTD* create(Attribute attribute, // What attribute is evalued.
                                Op op,               // What operator is used.
                                const char* zValue,               // The value from the rule file.
                                uint32_t debug);                  // Debug bits

protected:
    bool matches_column(const char* default_db, const GWBUF* query) const override;
    bool matches_table(const char* default_db, const GWBUF* query) const override;

    struct
    {
        std::string column;
        std::string table;
        std::string database;
    } m_ctd;

private:
    CacheRuleCTD(Attribute attribute, // What attribute is evalued.
                 Op op,               // What operator is used.
                 std::string value,                // The value from the rule file.
                 uint32_t debug)                   // Debug bits
        : CacheRuleSimple(attribute, op, value, debug)
    {
    }
};

class CacheRuleQuery final : public CacheRuleSimple
{
public:
    static CacheRuleQuery* create(Attribute attribute, // What attribute is evalued.
                                  Op op,               // What operator is used.
                                  const char* zValue,               // The value from the rule file.
                                  uint32_t debug);                  // Debug bits

private:
    CacheRuleQuery(Attribute attribute, // What attribute is evalued.
                   Op op,               // What operator is used.
                   std::string value,                // The value from the rule file.
                   uint32_t debug)                   // Debug bits
        : CacheRuleSimple(attribute, op, value, debug)
    {
    }
};

class CacheRuleRegex final : public CacheRuleValue
{
public:
    ~CacheRuleRegex();

    bool compare_n(const char* value, size_t length) const override final;

    static CacheRuleRegex* create(Attribute attribute, // What attribute is evalued.
                                  Op op,               // What operator is used.
                                  const char* zValue,               // The value from the rule file.
                                  uint32_t debug);                  // Debug bits

protected:
    bool matches_column(const char* default_db, const GWBUF* query) const override;
    bool matches_table(const char* default_db, const GWBUF* query) const override;

private:
    CacheRuleRegex(Attribute attribute, // What attribute is evalued.
                   Op op,               // What operator is used.
                   std::string value,                // The value from the rule file.
                   uint32_t debug)                   // Debug bits
        : CacheRuleValue(attribute, op, value, debug)
    {
        mxb_assert(op == Op::LIKE || op == Op::UNLIKE);
    }

    struct
    {
        pcre2_code* code;
    } m_regexp;
};

class CacheRuleUser final : public CacheRule
{
public:
    static CacheRuleUser* create(Attribute attribute, // What attribute is evalued.
                                 Op op,               // What operator is used.
                                 const char* zValue,               // The value from the rule file.
                                 uint32_t debug);                  // Debug bits

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

    bool matches_user(const char* account) const;

    bool compare(const std::string_view& value) const override;
    bool compare_n(const char* value, size_t length) const override;

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

    CacheRules(const CacheRules&) = delete;
    CacheRules& operator=(const CacheRules&) = delete;

    ~CacheRules();

    /**
     * Creates an empty rules object.
     *
     * @param debug The debug level.
     *
     * @return An empty rules object, or NULL in case of error.
     */
    static std::unique_ptr<CacheRules> create(uint32_t debug);

    /**
     * Parses the caching rules from a string.
     *
     * @param zJson  Null-terminate string containing JSON.
     * @param debug  The debug level.
     * @param pRules [out] The loaded rules.
     *
     * @return True, if the rules could be parsed, false otherwise.
     */
    static bool parse(const char* zJson, uint32_t debug, std::vector<SCacheRules>* pRules);

    /**
     * Loads the caching rules from a file.
     *
     * @param path   The path of the file containing the rules.
     * @param debug  The debug level.
     * @param pRules [out] The loaded rules.
     *
     * @return True, if the rules could be loaded, false otherwise.
     */
    static bool load(const char* zPath, uint32_t debug, std::vector<SCacheRules>* pRules);
    static bool load(const std::string& path, uint32_t debug, std::vector<SCacheRules>* pRules)
    {
        return load(path.c_str(), debug, pRules);
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
     * @param zdefault_db The current default database, NULL if there is none.
     * @param pquery      The query, expected to contain a COM_QUERY.
     *
     * @return True, if the results should be stored.
     */
    bool should_store(const char* zDefault_db, const GWBUF* pQuery) const;

    /**
     * Returns boolean indicating whether the cache should be used, that is consulted.
     *
     * @param psession  The current session.
     *
     * @return True, if the cache should be used.
     */
    bool should_use(const MXS_SESSION* pSession) const;

private:
    friend class Tester;

    CacheRules(uint32_t debug);

    static bool create_from_json(json_t* root, uint32_t debug, std::vector<SCacheRules>* pRules);
    static CacheRules* create_from_json(json_t* root, uint32_t debug);

    bool parse_json(json_t* root);

    using ElementParser = bool (CacheRules::*)(json_t* object, size_t index);

    bool parse_array(json_t* root, const char* name, ElementParser);

    bool parse_store_element(json_t* object, size_t index);
    bool parse_use_element(json_t* object, size_t index);

public: // TEMPORARY
    struct AttributeMapping
    {
        const char*          name;
        CacheRule::Attribute value;
    };

private:

    static AttributeMapping s_store_attributes[];
    static AttributeMapping s_use_attributes[];

    CacheRule* parse_element(json_t* object,
                             const char* name,
                             size_t index,
                             const AttributeMapping* pAttributes);

    using SCacheRuleValue = std::unique_ptr<CacheRuleValue>;
    using SCacheRuleUser = std::unique_ptr<CacheRuleUser>;

    using SCacheRuleValueVector = std::vector<SCacheRuleValue>;
    using SCacheRuleUserVector = std::vector<SCacheRuleUser>;

    json_t*               m_pRoot { nullptr }; // The JSON root object.
    uint32_t              m_debug { 0 };      // The debug level.
    SCacheRuleValueVector m_store_rules;      // The rules for when to store data to the cache.
    SCacheRuleUserVector  m_use_rules;        // The rules for when to use data from the cache.
};
