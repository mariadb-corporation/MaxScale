/*
 * Copyright (c) 2016 MariaDB Corporation Ab
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

#define MXB_MODULE_NAME "masking"
#include "maskingrules.hh"
#include <algorithm>
#include <errno.h>
#include <functional>
#include <string.h>
#include <maxbase/assert.hh>
#include <maxbase/jansson.hh>
#include <maxbase/string.hh>
#include <maxscale/pcre2.hh>
#include <maxscale/utils.hh>
#include <maxscale/json_api.hh>
#include <maxscale/protocol/mariadb/maxscale.hh>

using std::unique_ptr;
using std::string;
using std::vector;
using mxs::Parser;

namespace
{

static const char MASKING_DEFAULT_FILL[] = "X";

static const char KEY_APPLIES_TO[] = "applies_to";
static const char KEY_COLUMN[] = "column";
static const char KEY_DATABASE[] = "database";
static const char KEY_EXEMPTED[] = "exempted";
static const char KEY_FILL[] = "fill";
static const char KEY_REPLACE[] = "replace";
static const char KEY_RULES[] = "rules";
static const char KEY_TABLE[] = "table";
static const char KEY_VALUE[] = "value";
static const char KEY_WITH[] = "with";
static const char KEY_OBFUSCATE[] = "obfuscate";
static const char KEY_MATCH[] = "match";

/**
 * @class AccountVerbatim
 *
 * Implementation of @c MaskingRules::Rule::Account that compares user and
 * host names verbatim, that is, without regexp matching.
 */
class AccountVerbatim : public MaskingRules::Rule::Account
{
public:
    ~AccountVerbatim()
    {
    }

    static unique_ptr<MaskingRules::Rule::Account> create(const string& user, const string& host)
    {
        return unique_ptr<MaskingRules::Rule::Account>(new AccountVerbatim(user, host));
    }

    string user() const override
    {
        return m_user;
    }

    string host() const override
    {
        return m_host;
    }

    bool matches(const char* zUser, const char* zHost) const override
    {
        mxb_assert(zUser);
        mxb_assert(zHost);

        return (m_user.empty() || (m_user == zUser))
               && (m_host.empty() || (m_host == zHost));
    }

private:
    AccountVerbatim(const string& user, const string& host)
        : m_user(user)
        , m_host(host)
    {
    }

    AccountVerbatim(const AccountVerbatim&);
    AccountVerbatim& operator=(const AccountVerbatim&);

private:
    string m_user;
    string m_host;
};


/**
 * @class AccountRegexp
 *
 * Implementation of @c MaskingRules::Rule::Account that compares user names
 * verbatim, that is, without regexp matching, and host names using regexp
 * matching.
 */
class AccountRegexp : public MaskingRules::Rule::Account
{
public:
    ~AccountRegexp()
    {
        pcre2_code_free(m_pCode);
    }

    static unique_ptr<MaskingRules::Rule::Account> create(const string& user, const string& host)
    {
        unique_ptr<MaskingRules::Rule::Account> sAccount;

        int errcode;
        PCRE2_SIZE erroffset;
        pcre2_code* pCode = pcre2_compile((PCRE2_SPTR)host.c_str(),
                                          PCRE2_ZERO_TERMINATED,
                                          0,
                                          &errcode,
                                          &erroffset,
                                          NULL);

        if (pCode)
        {
            sAccount.reset(new AccountRegexp(user, host, pCode));
        }
        else
        {
            PCRE2_UCHAR errbuf[512];
            pcre2_get_error_message(errcode, errbuf, sizeof(errbuf));
            MXB_ERROR("Regex compilation failed at %d for regex '%s': %s",
                      (int)erroffset,
                      host.c_str(),
                      errbuf);
        }

        return sAccount;
    }

    string user() const override
    {
        return m_user;
    }

    string host() const override
    {
        return m_host;
    }

    bool matches(const char* zUser, const char* zHost) const override
    {
        mxb_assert(zUser);
        mxb_assert(zHost);

        bool rv = (m_user.empty() || (m_user == zUser));

        if (rv)
        {
            mxb_assert(m_pCode);
            pcre2_match_data* pData = pcre2_match_data_create_from_pattern(m_pCode, NULL);

            if (pData)
            {
                rv = (pcre2_match(m_pCode, (PCRE2_SPTR)zHost, PCRE2_ZERO_TERMINATED, 0, 0, pData, NULL) >= 0);
                pcre2_match_data_free(pData);
            }
        }

        return rv;
    }

private:
    AccountRegexp(const string& user,
                  const string& host,
                  pcre2_code* pCode)
        : m_user(user)
        , m_host(host)
        , m_pCode(pCode)
    {
    }

    AccountRegexp(const AccountRegexp&);
    AccountRegexp& operator=(const AccountRegexp&);

private:
    string      m_user;
    string      m_host;
    pcre2_code* m_pCode;
};

/**
 * Converts a list of account names into a vector of Account instances.
 *
 * @param zName     The key of the JSON array we are processing (error reporting).
 * @param pString   A JSON array of account names.
 * @param accounts  Vector of Account instances, to be filled by this function.
 *
 * @return True, if all account names could be converted, false otherwise.
 */
bool get_accounts(const char* zName,
                  json_t* pStrings,
                  vector<unique_ptr<MaskingRules::Rule::Account>>& accounts)
{
    mxb_assert(json_is_array(pStrings));

    bool success = true;

    size_t n = json_array_size(pStrings);
    size_t i = 0;

    while (success && (i < n))
    {
        json_t* pString = json_array_get(pStrings, i);
        mxb_assert(pString);

        if (json_is_string(pString))
        {
            auto sAccount = MaskingRules::Rule::Account::create(json_string_value(pString));

            if (sAccount)
            {
                accounts.push_back(std::move(sAccount));
            }
            else
            {
                success = false;
            }
        }
        else
        {
            MXB_ERROR("An element in a '%s' array is not a string.", zName);
            success = false;
        }

        ++i;
    }

    return success;
}

/**
 * Create all MaskingRules::Rule instances
 *
 * @param pRules  A JSON array representing 'rules' from the rules file.
 * @param rules   Vector where corresponding Rule instances will be pushed.
 *
 * @return True, if all rules could be created.
 */
bool create_rules_from_array(json_t* pRules, vector<unique_ptr<MaskingRules::Rule>>& rules)
{
    mxb_assert(json_is_array(pRules));

    bool parsed = true;

    size_t n = json_array_size(pRules);
    size_t i = 0;

    while (parsed && (i < n))
    {
        json_t* pRule = json_array_get(pRules, i);
        mxb_assert(pRule);

        if (json_is_object(pRule))
        {
            unique_ptr<MaskingRules::Rule> sRule;
            json_t* pObfuscate = json_object_get(pRule, KEY_OBFUSCATE);
            json_t* pReplace = json_object_get(pRule, KEY_REPLACE);

            // Check whether we have KEY_OBFUSCATE or KEY_REPLACE
            if (!pReplace && !pObfuscate)
            {
                MXB_ERROR("A masking rule does not contain a '%s' or '%s' key.",
                          KEY_OBFUSCATE,
                          KEY_REPLACE);
                parsed = false;
                continue;
            }

            // Obfuscate takes the precedence
            if (pObfuscate)
            {
                sRule = MaskingRules::ObfuscateRule::create_from(pRule);
            }
            else
            {
                json_t* pMatch = json_object_get(pReplace, KEY_MATCH);
                // Match takes the precedence
                sRule = pMatch ?
                    MaskingRules::MatchRule::create_from(pRule) :
                    MaskingRules::ReplaceRule::create_from(pRule);
            }

            if (sRule.get())
            {
                rules.push_back(std::move(sRule));
            }
            else
            {
                parsed = false;
            }
        }
        else
        {
            MXB_ERROR("Element %lu of the '%s' array is not an object.", i, KEY_RULES);
            parsed = false;
        }

        ++i;
    }

    return parsed;
}

/**
 * Create all MaskingRules::Rule instances
 *
 * @param pRoo   A JSON object, representing the rules file.
 * @param rules  Vector where all Rule instances will be pushed.
 *
 * @return True, if all rules could be created.
 */
bool create_rules_from_root(json_t* pRoot,
                            vector<unique_ptr<MaskingRules::Rule>>& rules)
{
    bool parsed = false;
    json_t* pRules = json_object_get(pRoot, KEY_RULES);

    if (pRules)
    {
        if (json_is_array(pRules))
        {
            parsed = create_rules_from_array(pRules, rules);
        }
        else
        {
            MXB_ERROR("The masking rules object contains a `%s` key, "
                      "but it is not an array.",
                      KEY_RULES);
        }
    }

    return parsed;
}

inline bool is_same_name(std::string lhs, std::string_view rhs)
{
    return mxb::sv_case_eq(lhs, rhs);
}

inline bool is_same_name(std::string lhs, const mxq::LEncString& rhs)
{
    return rhs.case_eq(lhs);
}
}

//
// MaskingRules::Rule::Account
//

MaskingRules::Rule::Account::Account()
{
}

MaskingRules::Rule::Account::~Account()
{
}

unique_ptr<MaskingRules::Rule::Account> MaskingRules::Rule::Account::create(const char* zAccount)
{
    unique_ptr<MaskingRules::Rule::Account> sAccount;

    size_t len = strlen(zAccount);
    char account[len + 1];
    strcpy(account, zAccount);

    char* zAt = strchr(account, '@');
    char* zUser = account;
    char* zHost = NULL;

    if (zAt)
    {
        *zAt = 0;
        zHost = zAt + 1;
    }

    if (mariadb::trim_quotes(zUser))
    {
        char pcre_host[2 * len + 1];    // Surely enough

        mxs_mysql_name_kind_t rv = MXS_MYSQL_NAME_WITHOUT_WILDCARD;

        if (zHost)
        {
            if (mariadb::trim_quotes(zHost))
            {
                rv = mxs_mysql_name_to_pcre(pcre_host, zHost, MXS_PCRE_QUOTE_WILDCARD);

                if (rv == MXS_MYSQL_NAME_WITH_WILDCARD)
                {
                    zHost = pcre_host;
                }
            }
            else
            {
                MXB_ERROR("Could not trim quotes from host part of %s.", zAccount);
                zHost = NULL;
            }
        }
        else
        {
            zHost = const_cast<char*>("");
        }

        if (zHost)
        {
            if (rv == MXS_MYSQL_NAME_WITH_WILDCARD)
            {
                sAccount = AccountRegexp::create(zUser, zHost);
            }
            else
            {
                sAccount = AccountVerbatim::create(zUser, zHost);
            }
        }
    }
    else
    {
        MXB_ERROR("Could not trim quotes from user part of %s.", zAccount);
    }

    return sAccount;
}

//
// MaskingRules::Rule
//

MaskingRules::Rule::Rule(const std::string& column,
                         const std::string& table,
                         const std::string& database,
                         std::vector<SAccount>&& applies_to,
                         std::vector<SAccount>&& exempted)
    : m_column(column)
    , m_table(table)
    , m_database(database)
    , m_applies_to(std::move(applies_to))
    , m_exempted(std::move(exempted))
{
}

MaskingRules::ReplaceRule::ReplaceRule(const std::string& column,
                                       const std::string& table,
                                       const std::string& database,
                                       std::vector<SAccount>&& applies_to,
                                       std::vector<SAccount>&& exempted,
                                       const std::string& value,
                                       const std::string& fill)
    : MaskingRules::Rule::Rule(column, table, database, std::move(applies_to), std::move(exempted))
    , m_value(value)
    , m_fill(fill)
{
}

MaskingRules::ObfuscateRule::ObfuscateRule(const std::string& column,
                                           const std::string& table,
                                           const std::string& database,
                                           std::vector<SAccount>&& applies_to,
                                           std::vector<SAccount>&& exempted)
    : MaskingRules::Rule::Rule(column, table, database, std::move(applies_to), std::move(exempted))
{
}

MaskingRules::MatchRule::MatchRule(const std::string& column,
                                   const std::string& table,
                                   const std::string& database,
                                   std::vector<SAccount>&& applies_to,
                                   std::vector<SAccount>&& exempted,
                                   pcre2_code* regexp,
                                   const std::string& value,
                                   const std::string& fill)
    : MaskingRules::Rule::Rule(column, table, database, std::move(applies_to), std::move(exempted))
    , m_regexp(regexp)
    , m_value(value)
    , m_fill(fill)
{
}

MaskingRules::Rule::~Rule()
{
}

MaskingRules::ReplaceRule::~ReplaceRule()
{
}

MaskingRules::ObfuscateRule::~ObfuscateRule()
{
}

MaskingRules::MatchRule::~MatchRule()
{
    pcre2_code_free(m_regexp);
}

/** Check the Json array for user rules
 *
 * @param pApplies_to    The array of users the rule is applied to
 * @param pExempted      The array of users the rule is NOT applied to
 *
 * @return               False on errors, True otherwise
 */
static bool validate_user_rules(json_t* pApplies_to, json_t* pExempted)
{
    const char* err = NULL;
    // Check for pApplies_to and pExempted
    if (pApplies_to && !json_is_array(pApplies_to))
    {
        err = KEY_APPLIES_TO;
    }

    if (pExempted && !json_is_array(pExempted))
    {
        err = KEY_EXEMPTED;
    }

    if (err)
    {
        MXB_ERROR("A masking rule contains a '%s' key, "
                  "but the value is not an array.",
                  err);
        return false;
    }

    return true;
}

static json_t* rule_get_object(json_t* pRule,
                               const char* rule_type)
{
    json_t* pObj = NULL;
    // Check 'rule_type' object
    if (!pRule || !(pObj = json_object_get(pRule, rule_type)))
    {
        MXB_ERROR("A masking rule does not contain the '%s' key.",
                  rule_type);
        return NULL;
    }
    if (!json_is_object(pObj))
    {
        MXB_ERROR("A masking rule contains a '%s' key, "
                  "but the value is not a valid Json object.",
                  rule_type);
        return NULL;
    }
    return pObj;
}

/**
 * Checks database, table and column values
 *
 * @param pColumn      The database column
 * @param pTable       The database table
 * @param pDatabase    The database name
 * @param rule_type    The rule type
 *
 * @return           true on success, false otherwise
 */
static bool rule_check_database_options(json_t* pColumn,
                                        json_t* pTable,
                                        json_t* pDatabase,
                                        const char* rule_type)
{

    // Only column is mandatory; both table and database are optional.
    if ((pColumn && json_is_string(pColumn))
        && (!pTable || json_is_string(pTable))
        && (!pDatabase || json_is_string(pDatabase)))
    {
        return true;
    }
    else
    {
        if (!pColumn || !json_is_string(pColumn))
        {
            MXB_ERROR("A masking rule '%s' does not have "
                      "the mandatory '%s' key or it's not a valid Json string.",
                      rule_type,
                      KEY_COLUMN);
        }
        else
        {
            MXB_ERROR("In a masking rule '%s', the keys "
                      "'%s' and/or '%s' are not valid Json strings.",
                      rule_type,
                      KEY_TABLE,
                      KEY_DATABASE);
        }
        return false;
    }
}

/**
 * Returns a Json objet with the fill value
 *
 * @param pDoc    The Json input object
 *
 * @return        A Json object or NULL
 */
static json_t* rule_get_fill(json_t* pDoc)
{
    json_t* pFill = json_object_get(pDoc, KEY_FILL);

    if (!pFill)
    {
        // Allowed. Use default value for fill and add it to pWith.
        pFill = json_string(MASKING_DEFAULT_FILL);
        if (pFill)
        {
            json_object_set_new(pDoc, KEY_FILL, pFill);
        }
        else
        {
            MXB_ERROR("json_string() error, cannot produce"
                      " a valid '%s' object for rule '%s'.",
                      KEY_FILL,
                      KEY_REPLACE);
        }
    }

    return pFill;
}

/**
 * Perform rule checks for all Rule classes
 *
 * @param pRule         The Json rule
 * @param applies_to    Account instances corresponding to the
 *                      accounts listed in 'applies_to' in the json file.
 * @param exempted      Account instances corresponding to the
 *                      accounts listed in 'exempted' in the json file.
 *
 * @return              True on success, false on errors.
 */
static bool rule_run_common_checks(json_t* pRule,
                                   vector<unique_ptr<MaskingRules::Rule::Account>>* applies_to,
                                   vector<unique_ptr<MaskingRules::Rule::Account>>* exempted)
{
    json_t* pApplies_to = json_object_get(pRule, KEY_APPLIES_TO);
    json_t* pExempted = json_object_get(pRule, KEY_EXEMPTED);

    // Check for pApplies_to and pExempted
    if (!validate_user_rules(pApplies_to, pExempted))
    {
        return false;
    }

    // Set the account rules
    if ((pApplies_to && !get_accounts(KEY_APPLIES_TO, pApplies_to, *applies_to))
        || (pExempted && !get_accounts(KEY_EXEMPTED, pExempted, *exempted)))
    {
        return false;
    }

    return true;
}

/**
 * Returns rule values from a Json rule object
 *
 * @param pRule         The Json rule
 * @param column        The column value from the json file.
 * @param table         The table value from the json file.
 * @param database      The database value from the json file.
 * @param rule_type     The rule type
 *
 * @return              True on success, false on errors.
 */
static bool rule_get_common_values(json_t* pRule,
                                   std::string* column,
                                   std::string* table,
                                   std::string* database,
                                   const char* rule_type)
{
    // Get database, table && column
    json_t* pDatabase = json_object_get(pRule, KEY_DATABASE);
    json_t* pTable = json_object_get(pRule, KEY_TABLE);
    json_t* pColumn = json_object_get(pRule, KEY_COLUMN);

    // Check column/table/database
    if (!rule_check_database_options(pColumn,
                                     pTable,
                                     pDatabase,
                                     rule_type))
    {
        return false;
    }

    // Column exists
    column->assign(json_string_value(pColumn));

    // Check optional table and dbname
    if (pTable)
    {
        table->assign(json_string_value(pTable));
    }
    if (pDatabase)
    {
        database->assign(json_string_value(pDatabase));
    }

    return true;
}

/**
 * Check Json object, run common checks and return rule values
 *
 * @param pRule         The Json rule object
 * @param applies_to    Account instances corresponding to the
 *                      accounts listed in 'applies_to' in the json file.
 * @param exempted      Account instances corresponding to the
 *                      accounts listed in 'exempted' in the json file.
 * @param column        The column value from the json file.
 * @param table         The table value from the json file.
 * @param database      The database value from the json file.
 * @param rule_type     The rule_type (obfuscate or replace)
 *
 * @return              True on success, false on errors
 */
bool rule_get_values(json_t* pRule,
                     vector<unique_ptr<MaskingRules::Rule::Account>>* applies_to,
                     vector<unique_ptr<MaskingRules::Rule::Account>>* exempted,
                     std::string* column,
                     std::string* table,
                     std::string* database,
                     const char* rule_type)
{
    json_t* pKeyObj;
    // Get Key object based on 'rule_type' param
    if ((pKeyObj = rule_get_object(pRule,
                                   rule_type))
        &&      // Run checks on user access
        rule_run_common_checks(pRule,
                               applies_to,
                               exempted)
        &&      // Extract values from the rule
        rule_get_common_values(pKeyObj,
                               column,
                               table,
                               database,
                               rule_type))
    {
        return true;
    }

    return false;
}

/**
 * Returns 'match' regexp & 'fill' value from a 'replace' rule
 *
 * @param pRule       The Json rule doc
 * @param pMatch      The string buffer for 'match'value
 * @param pValue      The string buffer for 'value' value
 * @param pFill       The string buffer for 'fill' value
 *
 * @return            True on success, false on errors
 */
bool rule_get_match_value_fill(json_t* pRule,
                               std::string* pMatch,
                               std::string* pValue,
                               std::string* pFill)
{
    // Get the 'with' key from the rule
    json_t* pWith = json_object_get(pRule, KEY_WITH);
    if (!pWith || !json_is_object(pWith))
    {
        MXB_ERROR("A masking '%s' rule doesn't have a valid '%s' key",
                  KEY_REPLACE,
                  KEY_WITH);
        return false;
    }

    // Get the 'replace' rule object
    json_t* pKeyObj;
    if (!(pKeyObj = rule_get_object(pRule, KEY_REPLACE)))
    {
        return false;
    }

    // Get fill from 'with' object
    json_t* pTheFill = rule_get_fill(pWith);
    // Get value from 'with' object
    json_t* pTheValue = json_object_get(pWith, KEY_VALUE);
    // Get 'match' from 'replace' object
    json_t* pTheMatch = json_object_get(pKeyObj, KEY_MATCH);

    // Check values: 'match' and 'fill' are mandatory (if not provided, there will be
    // a default 'fill'), while 'value' is optional, but if provided it must be a string.
    if ((!pTheFill || !json_is_string(pTheFill))
        || (pTheValue && !json_is_string(pTheValue))
        || ((!pTheMatch || !json_is_string(pTheMatch))))
    {
        MXB_ERROR("A masking '%s' rule has '%s', '%s' and/or '%s' "
                  "invalid Json strings.",
                  KEY_REPLACE,
                  KEY_MATCH,
                  KEY_VALUE,
                  KEY_FILL);
        return false;
    }
    else
    {
        // Update the string buffers
        pFill->assign(json_string_value(pTheFill));
        pMatch->assign(json_string_value(pTheMatch));

        if (pTheValue)
        {
            pValue->assign(json_string_value(pTheValue));
        }

        return true;
    }
}

/**
 * Returns 'value' & 'fill' from a 'replace' rule
 *
 * @param pRule     The Json rule doc
 * @param pValue    The string buffer for 'value'
 * @param pFill     The string buffer for 'fill'
 *
 * @return            True on success, false on errors
 */
bool rule_get_value_fill(json_t* pRule,
                         std::string* pValue,
                         std::string* pFill)
{
    // Get the 'with' key from the rule
    json_t* pWith = json_object_get(pRule, KEY_WITH);
    if (!pWith || !json_is_object(pWith))
    {
        MXB_ERROR("A masking '%s' rule doesn't have a valid '%s' key.",
                  KEY_REPLACE,
                  KEY_WITH);
        return false;
    }

    // Get fill from 'with' object
    json_t* pTheFill = rule_get_fill(pWith);

    // Get value from 'with' object
    json_t* pTheValue = json_object_get(pWith, KEY_VALUE);

    // Check Json strings
    if ((pTheFill && !json_is_string(pTheFill))
        || (pTheValue && !json_is_string(pTheValue)))
    {
        MXB_ERROR("A masking '%s' rule has '%s' and/or '%s' "
                  "invalid Json strings.",
                  KEY_REPLACE,
                  KEY_VALUE,
                  KEY_FILL);
        return false;
    }

    // Update the string values
    if (pTheFill)
    {
        pFill->assign(json_string_value(pTheFill));
    }
    if (pTheValue)
    {
        pValue->assign(json_string_value(pTheValue));
    }

    return true;
}

// static
unique_ptr<MaskingRules::Rule> MaskingRules::ReplaceRule::create_from(json_t* pRule)
{
    mxb_assert(json_is_object(pRule));

    json_t* pReplace;
    std::string column, table, database, value, fill;
    vector<unique_ptr<MaskingRules::Rule::Account>> applies_to;
    vector<unique_ptr<MaskingRules::Rule::Account>> exempted;
    unique_ptr<MaskingRules::Rule> sRule;

    // Check rule, extract base values
    if (rule_get_values(pRule,
                        &applies_to,
                        &exempted,
                        &column,
                        &table,
                        &database,
                        KEY_REPLACE)
        && rule_get_value_fill(pRule, &value, &fill))   // get value/fill
    {
        // Apply value/fill: instantiate the ReplaceRule class
        sRule = unique_ptr<MaskingRules::ReplaceRule>(new MaskingRules::ReplaceRule(column,
                                                                                    table,
                                                                                    database,
                                                                                    std::move(applies_to),
                                                                                    std::move(exempted),
                                                                                    value,
                                                                                    fill));
    }

    return sRule;
}

// static
unique_ptr<MaskingRules::Rule> MaskingRules::ObfuscateRule::create_from(json_t* pRule)
{
    mxb_assert(json_is_object(pRule));

    std::string column, table, database;
    vector<unique_ptr<MaskingRules::Rule::Account>> applies_to;
    vector<unique_ptr<MaskingRules::Rule::Account>> exempted;
    unique_ptr<MaskingRules::Rule> sRule;

    // Check rule, extract base values
    if (rule_get_values(pRule,
                        &applies_to,
                        &exempted,
                        &column,
                        &table,
                        &database,
                        KEY_OBFUSCATE))
    {
        sRule = unique_ptr<MaskingRules::Rule>(new MaskingRules::ObfuscateRule(column,
                                                                               table,
                                                                               database,
                                                                               std::move(applies_to),
                                                                               std::move(exempted)));
    }

    return sRule;
}

/**
 * Compiles a pcre2 pattern match
 *
 * @param match_string    The pattern match to compile
 *
 * @return                A valid pcre2_code code or NULL on errors.
 */
static pcre2_code* rule_compile_pcre2_match(const char* match_string)
{
    int errcode;
    PCRE2_SIZE erroffset;
    // Compile regexp
    pcre2_code* pCode = pcre2_compile((PCRE2_SPTR)match_string,
                                      PCRE2_ZERO_TERMINATED,
                                      0,
                                      &errcode,
                                      &erroffset,
                                      NULL);
    if (!pCode)
    {
        PCRE2_UCHAR errbuf[512];
        pcre2_get_error_message(errcode, errbuf, sizeof(errbuf));
        MXB_ERROR("Regex compilation failed at %d for regex '%s': %s",
                  (int)erroffset,
                  match_string,
                  errbuf);
        return NULL;
    }

    return pCode;
}

// static
unique_ptr<MaskingRules::Rule> MaskingRules::MatchRule::create_from(json_t* pRule)
{
    mxb_assert(json_is_object(pRule));

    std::string column, table, database, value, fill, match;
    vector<unique_ptr<MaskingRules::Rule::Account>> applies_to;
    vector<unique_ptr<MaskingRules::Rule::Account>> exempted;
    unique_ptr<MaskingRules::Rule> sRule;

    // Check rule, extract base values
    // Note: the match rule has same rule_type of "replace"
    if (rule_get_values(pRule,
                        &applies_to,
                        &exempted,
                        &column,
                        &table,
                        &database,
                        KEY_REPLACE)
        && rule_get_match_value_fill(pRule,     // get match/value/fill
                                     &match,
                                     &value,
                                     &fill))
    {

        if (!match.empty() && !fill.empty())
        {
            // Compile the regexp
            pcre2_code* pCode = rule_compile_pcre2_match(match.c_str());

            if (pCode)
            {
                // Instantiate the MatchRule class
                sRule = unique_ptr<MaskingRules::MatchRule>(new MaskingRules::MatchRule(column,
                                                                                        table,
                                                                                        database,
                                                                                        std::move(applies_to),
                                                                                        std::move(exempted),
                                                                                        pCode,
                                                                                        value,
                                                                                        fill));
            }
        }
    }

    return sRule;
}

string MaskingRules::Rule::match() const
{
    string s;

    s += m_database.empty() ? "*" : m_database;
    s += ".";
    s += m_table.empty() ? "*" : m_table;
    s += ".";
    s += m_column;

    return s;
}

bool MaskingRules::Rule::matches(const ComQueryResponse::ColumnDef& column_def,
                                 const char* zUser,
                                 const char* zHost) const
{
    const mxq::LEncString& table = column_def.org_table();
    const mxq::LEncString& database = column_def.schema();

    // If the resultset does not contain table and database names, as will
    // be the case in e.g. "SELECT * FROM table UNION SELECT * FROM table",
    // we consider it a match if a table or database have been provided.
    // Otherwise it would be easy to bypass a table/database rule.

    bool match =
        is_same_name(m_column, column_def.org_name())
        && (m_table.empty() || table.empty() || is_same_name(m_table, table))
        && (m_database.empty() || database.empty() || is_same_name(m_database, database));

    if (match)
    {
        // If the column matched, then we need to check whether the rule applies
        // to the user and host.

        match = matches_account(zUser, zHost);
    }

    return match;
}

bool MaskingRules::Rule::matches(const Parser::FieldInfo& field,
                                 const char* zUser,
                                 const char* zHost) const
{
    std::string_view column = field.column;
    std::string_view table = field.table;
    std::string_view database = field.database;

    mxb_assert(!column.empty());

    // If the resultset does not contain table and database names, as will
    // be the case in e.g. "SELECT * FROM table UNION SELECT * FROM table",
    // we consider it a match if a table or database have been provided.
    // Otherwise it would be easy to bypass a table/database rule.

    bool match =
        is_same_name(m_column, column)
        && (m_table.empty() || table.empty() || is_same_name(m_table, table))
        && (m_database.empty() || database.empty() || is_same_name(m_database, database));

    if (match)
    {
        // If the column matched, then we need to check whether the rule applies
        // to the user and host.

        match = matches_account(zUser, zHost);
    }

    return match;
}

namespace
{

class AccountMatcher
{
public:
    AccountMatcher(const char* zUser, const char* zHost)
        : m_zUser(zUser)
        , m_zHost(zHost)
    {
    }

    bool operator()(const MaskingRules::Rule::SAccount& sAccount)
    {
        return sAccount->matches(m_zUser, m_zHost);
    }

private:
    const char* m_zUser;
    const char* m_zHost;
};
}

bool MaskingRules::Rule::matches_account(const char* zUser,
                                         const char* zHost) const
{
    bool match = true;

    AccountMatcher matcher(zUser, zHost);

    if (m_applies_to.size() != 0)
    {
        vector<SAccount>::const_iterator i = std::find_if(m_applies_to.begin(),
                                                          m_applies_to.end(),
                                                          matcher);

        match = (i != m_applies_to.end());
    }

    if (match && (m_exempted.size() != 0))
    {
        // If it is still a match, we need to check whether the user/host is
        // exempted.

        vector<SAccount>::const_iterator i = std::find_if(m_exempted.begin(),
                                                          m_exempted.end(),
                                                          matcher);

        match = (i == m_exempted.end());
    }

    return match;
}

/**
 * Fills a buffer with a fill string
 *
 * @param f_first    The iterator pointing to first fill byte
 * @param f_last     The iterator pointing to last fill byte
 * @param o_first    The iterator pointing to first buffer byte
 * @param o_last     The iterator pointing to last buffer byte
 */
template<class FillIter, class OutIter>
inline void fill_buffer(FillIter f_first,
                        FillIter f_last,
                        OutIter o_first,
                        OutIter o_last)
{
    FillIter pFill = f_first;
    while (o_first != o_last)
    {
        *o_first++ = *pFill++;
        if (pFill == f_last)
        {
            pFill = f_first;
        }
    }
}

void MaskingRules::MatchRule::rewrite(mxq::LEncString& s) const
{
    int rv = 0;
    uint32_t n_matches = 0;
    // Create the match data object from m_regexp class member
    pcre2_match_data* pData = pcre2_match_data_create_from_pattern(m_regexp, NULL);
    // Set initial offset to the input beginning
    PCRE2_SIZE startoffset = 0;
    // Get input string size
    size_t total_len = s.length();

    if (pData)
    {
        // Get the fill size
        size_t fill_len = m_fill.length();

        // Match all the compiled pattern
        while ((startoffset < total_len)
               && (rv = pcre2_match(m_regexp,
                                    (PCRE2_SPTR)s.to_string().c_str(),
                                    PCRE2_ZERO_TERMINATED,
                                    startoffset,
                                    0,
                                    pData,
                                    NULL)) >= 0)
        {
            // Get offset array value pairs of substrings: $0=0,1 ; $1=2,3
            PCRE2_SIZE* ovector = pcre2_get_ovector_pointer(pData);

            // Get Full Match substring size: $0 is [0] and [1]
            size_t substring_len = ovector[1] - ovector[0];
            // Go to Full Match substring offset: 0
            mxq::LEncString::iterator i = s.begin() + ovector[0];

            // Avoid infinite loop in pcre2_match for a zero-length match
            if (ovector[1] == ovector[0])
            {
                break;
            }

            // Copy the fill string into substring
            if (m_value.length() == substring_len)
            {
                std::copy(m_value.begin(), m_value.end(), i);
            }
            else
            {
                fill_buffer(m_fill.begin(), m_fill.end(), i, i + substring_len);
            }

            // Set offset to the end of Full Match substring or break
            startoffset = ovector[1];
        }

        pcre2_match_data_free(pData);

        // Log errors, excluding NO_MATCH or PARTIAL
        if (rv < 0 && (rv != PCRE2_ERROR_NOMATCH || PCRE2_ERROR_PARTIAL))
        {
            MXS_PCRE2_PRINT_ERROR(rv);
        }
    }
    else
    {
        MXB_ERROR("Allocation of matching data for PCRE2 failed."
                  " This is most likely caused by a lack of memory");
    }
}

void MaskingRules::ObfuscateRule::rewrite(mxq::LEncString& s) const
{
    size_t i_len = s.length();
    mxq::LEncString::iterator i = s.begin();
    size_t c = *i + i_len;

    for (; i != s.end(); i++)
    {
        // ASCII 32 is first printable char
        unsigned char d = abs((char)(*i ^ c)) + 32;
        c += d << 3;
        // ASCII 126 is last printable char
        *i = d <= 126 ? d : 126;
    }
}

void MaskingRules::ReplaceRule::rewrite(mxq::LEncString& s) const
{
    bool rewritten = false;

    size_t total_len = s.length();

    if (!m_value.empty())
    {
        if (m_value.length() == total_len)
        {
            std::copy(m_value.begin(), m_value.end(), s.begin());
            rewritten = true;
        }
    }

    if (!rewritten)
    {
        if (!m_fill.empty())
        {
            // Copy the fill string
            fill_buffer(m_fill.begin(), m_fill.end(), s.begin(), s.end());
        }
        else
        {
            MXB_ERROR("Length of returned value \"%s\" is %u, while length of "
                      "replacement value \"%s\" is %u, and no 'fill' value specified.",
                      s.to_string().c_str(),
                      (unsigned)s.length(),
                      m_value.c_str(),
                      (unsigned)m_value.length());
        }
    }
}

//
// MaskingRules
//

MaskingRules::MaskingRules(json_t* pRoot, std::vector<SRule>&& rules)
    : m_pRoot(pRoot)
    , m_rules(std::move(rules))
{
    json_incref(m_pRoot);
}

MaskingRules::~MaskingRules()
{
    json_decref(m_pRoot);
}

// static
unique_ptr<MaskingRules> MaskingRules::load(const char* zPath)
{
    unique_ptr<MaskingRules> sRules;

    FILE* pFile = fopen(zPath, "r");

    if (pFile)
    {
        json_error_t error;
        json_t* pRoot = json_loadf(pFile, JSON_DISABLE_EOF_CHECK, &error);
        fclose(pFile);

        if (pRoot)
        {
            std::unique_ptr<json_t> root(pRoot);

            sRules = create_from(root.get());
        }
        else
        {
            MXB_ERROR("Loading rules file failed: (%s:%d:%d): %s",
                      zPath,
                      error.line,
                      error.column,
                      error.text);
        }
    }
    else
    {
        MXB_ERROR("Could not open rules file %s for reading: %s",
                  zPath,
                  mxb_strerror(errno));
    }

    return sRules;
}

// static
unique_ptr<MaskingRules> MaskingRules::parse(const char* zJson)
{
    unique_ptr<MaskingRules> sRules;

    json_error_t error;
    json_t* pRoot = json_loads(zJson, JSON_DISABLE_EOF_CHECK, &error);

    if (pRoot)
    {
        std::unique_ptr<json_t> root(pRoot);

        sRules = create_from(root.get());
    }
    else
    {
        MXB_ERROR("Parsing rules failed: (%d:%d): %s",
                  error.line,
                  error.column,
                  error.text);
    }

    return sRules;
}

// static
std::unique_ptr<MaskingRules> MaskingRules::create_from(json_t* pRoot)
{
    unique_ptr<MaskingRules> sRules;

    vector<SRule> rules;

    if (create_rules_from_root(pRoot, rules))
    {
        sRules = unique_ptr<MaskingRules>(new MaskingRules(pRoot, std::move(rules)));
    }

    return sRules;
}

namespace
{

template<class T>
class RuleMatcher
{
public:
    RuleMatcher(const T& field,
                const char* zUser,
                const char* zHost)
        : m_field(field)
        , m_zUser(zUser)
        , m_zHost(zHost)
    {
    }

    bool operator()(const MaskingRules::SRule& sRule)
    {
        return sRule->matches(m_field, m_zUser, m_zHost);
    }

private:
    const T&    m_field;
    const char* m_zUser;
    const char* m_zHost;
};
}

const MaskingRules::Rule* MaskingRules::get_rule_for(const ComQueryResponse::ColumnDef& column_def,
                                                     const char* zUser,
                                                     const char* zHost) const
{
    const Rule* pRule = NULL;

    RuleMatcher<ComQueryResponse::ColumnDef> matcher(column_def, zUser, zHost);
    vector<SRule>::const_iterator i = std::find_if(m_rules.begin(), m_rules.end(), matcher);

    if (i != m_rules.end())
    {
        const SRule& sRule = *i;

        pRule = sRule.get();
    }

    return pRule;
}

const MaskingRules::Rule* MaskingRules::get_rule_for(const Parser::FieldInfo& field_info,
                                                     const char* zUser,
                                                     const char* zHost) const
{
    const Rule* pRule = NULL;

    RuleMatcher<Parser::FieldInfo> matcher(field_info, zUser, zHost);
    vector<SRule>::const_iterator i = std::find_if(m_rules.begin(), m_rules.end(), matcher);

    if (i != m_rules.end())
    {
        const SRule& sRule = *i;

        pRule = sRule.get();
    }

    return pRule;
}

bool MaskingRules::has_rule_for(const char* zUser, const char* zHost) const
{
    auto i = std::find_if(m_rules.begin(), m_rules.end(), [zUser, zHost](const SRule& sRule) {
                              return sRule->matches_account(zUser, zHost);
                          });

    return i != m_rules.end();
}
