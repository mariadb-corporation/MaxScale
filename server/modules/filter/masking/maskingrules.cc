/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2020-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#define MXS_MODULE_NAME "masking"
#include "maskingrules.hh"
#include <algorithm>
#include <errno.h>
#include <functional>
#include <string.h>
#include <maxscale/debug.h>
#include <maxscale/jansson.hh>
#include <maxscale/mysql_utils.h>
#include <maxscale/pcre2.hh>
#include <maxscale/utils.hh>

using std::auto_ptr;
using std::string;
using std::vector;
using std::tr1::shared_ptr;
using maxscale::Closer;

namespace
{

static const char KEY_APPLIES_TO[] = "applies_to";
static const char KEY_COLUMN[]     = "column";
static const char KEY_DATABASE[]   = "database";
static const char KEY_EXEMPTED[]   = "exempted";
static const char KEY_FILL[]       = "fill";
static const char KEY_REPLACE[]    = "replace";
static const char KEY_RULES[]      = "rules";
static const char KEY_TABLE[]      = "table";
static const char KEY_VALUE[]      = "value";
static const char KEY_WITH[]       = "with";

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

    static shared_ptr<MaskingRules::Rule::Account> create(const string& user, const string& host)
    {
        return shared_ptr<MaskingRules::Rule::Account>(new AccountVerbatim(user, host));
    }

    string user() const
    {
        return m_user;
    }

    string host() const
    {
        return m_host;
    }

    bool matches(const char* zUser, const char* zHost) const
    {
        ss_dassert(zUser);
        ss_dassert(zHost);

        return
            (m_user.empty() || (m_user == zUser)) &&
            (m_host.empty() || (m_host == zHost));
    }

private:
    AccountVerbatim(const string& user, const string& host)
        : m_user(user)
        , m_host(host)
    {
    }

    AccountVerbatim(const AccountVerbatim&);
    AccountVerbatim& operator = (const AccountVerbatim&);

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

    static shared_ptr<MaskingRules::Rule::Account> create(const string& user, const string& host)
    {
        shared_ptr<MaskingRules::Rule::Account> sAccount;

        int errcode;
        PCRE2_SIZE erroffset;
        pcre2_code* pCode = pcre2_compile((PCRE2_SPTR)host.c_str(), PCRE2_ZERO_TERMINATED, 0,
                                          &errcode, &erroffset, NULL);

        if (pCode)
        {
            Closer<pcre2_code*> code(pCode);

            sAccount = shared_ptr<AccountRegexp>(new AccountRegexp(user, host, pCode));

            // Ownership of pCode has been moved to the AccountRegexp object.
            code.release();
        }
        else
        {
            PCRE2_UCHAR errbuf[512];
            pcre2_get_error_message(errcode, errbuf, sizeof(errbuf));
            MXS_ERROR("Regex compilation failed at %d for regex '%s': %s",
                      (int)erroffset, host.c_str(), errbuf);
        }

        return sAccount;
    }

    string user() const
    {
        return m_user;
    }

    string host() const
    {
        return m_host;
    }

    bool matches(const char* zUser, const char* zHost) const
    {
        ss_dassert(zUser);
        ss_dassert(zHost);

        bool rv = (m_user.empty() || (m_user == zUser));

        if (rv)
        {
            ss_dassert(m_pCode);
            pcre2_match_data* pData = pcre2_match_data_create_from_pattern(m_pCode, NULL);

            if (pData)
            {
                Closer<pcre2_match_data*> data(pData);

                rv = (pcre2_match(m_pCode, (PCRE2_SPTR)zHost, 0, 0, 0, pData, NULL) >= 0);
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
    AccountRegexp& operator = (const AccountRegexp&);

private:
    string      m_user;
    string      m_host;
    pcre2_code* m_pCode;
};

/**
 * Create MaxskingRules::Rule::Account instance
 *
 * @param zAccount  The account name as specified in the JSON rules file.
 *
 * @return Either an AccountVerbatim or AccountRegexp, depending on whether
 *         the provided account name contains wildcards or not.
 */
shared_ptr<MaskingRules::Rule::Account> create_account(const char* zAccount)
{
    shared_ptr<MaskingRules::Rule::Account> sAccount;

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

    if (mxs_mysql_trim_quotes(zUser))
    {
        char pcre_host[2 * len + 1]; // Surely enough

        mxs_mysql_name_kind_t rv = MXS_MYSQL_NAME_WITHOUT_WILDCARD;

        if (zHost)
        {
            if (mxs_mysql_trim_quotes(zHost))
            {
                rv = mxs_mysql_name_to_pcre(pcre_host, zHost, MXS_PCRE_QUOTE_WILDCARD);

                if (rv == MXS_MYSQL_NAME_WITH_WILDCARD)
                {
                    zHost = pcre_host;
                }
            }
            else
            {
                MXS_ERROR("Could not trim quotes from host part of %s.", zAccount);
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
        MXS_ERROR("Could not trim quotes from user part of %s.", zAccount);
    }

    return sAccount;
}

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
                  vector<shared_ptr<MaskingRules::Rule::Account> >& accounts)
{
    ss_dassert(json_is_array(pStrings));

    bool success = true;

    size_t n = json_array_size(pStrings);
    size_t i = 0;

    while (success && (i < n))
    {
        json_t* pString = json_array_get(pStrings, i);
        ss_dassert(pString);

        if (json_is_string(pString))
        {
            shared_ptr<MaskingRules::Rule::Account> sAccount = create_account(json_string_value(pString));

            if (sAccount)
            {
                accounts.push_back(sAccount);
            }
            else
            {
                success = false;
            }
        }
        else
        {
            MXS_ERROR("An element in a '%s' array is not a string.", zName);
            success = false;
        }

        ++i;
    }

    return success;
}

/**
 * Create a MaskingRules::Rule instance
 *
 * @param pColumn      A JSON string containing a column name.
 * @param pTable       A JSON string containing a table name, or NULL.
 * @param pDatabase    A JSON string containing a table name, or NULL.
 * @param pValue       A JSON string representing the 'value' of a 'with' object from the rules file.
 * @param pFill        A JSON string representing the 'fill' of a 'with' object from the rules file.
 * @param pApplies_to  A JSON array representing the 'applies_to' account names.
 * @param pExempted    A JSON array representing the 'exempted' account names.
 *
 * @return A Rule instance or NULL in case of error.
 */
auto_ptr<MaskingRules::ReplaceRule> create_rule_from_elements(json_t* pColumn,
                                                              json_t* pTable,
                                                              json_t* pDatabase,
                                                              json_t* pValue,
                                                              json_t* pFill,
                                                              json_t* pApplies_to,
                                                              json_t* pExempted)
{
    ss_dassert(pColumn && json_is_string(pColumn));
    ss_dassert(!pTable || json_is_string(pTable));
    ss_dassert(!pDatabase || json_is_string(pDatabase));
    ss_dassert(pValue || pFill);
    ss_dassert(!pValue || json_is_string(pValue));
    ss_dassert(!pFill || json_is_string(pFill));
    ss_dassert(!pApplies_to || json_is_array(pApplies_to));
    ss_dassert(!pExempted || json_is_array(pExempted));

    auto_ptr<MaskingRules::ReplaceRule> sRule;

    string column(json_string_value(pColumn));
    string table(pTable ? json_string_value(pTable) : "");
    string database(pDatabase ? json_string_value(pDatabase) : "");
    string value(pValue ? json_string_value(pValue) : "");
    string fill(pFill ? json_string_value(pFill) : "");

    bool ok = true;
    vector<shared_ptr<MaskingRules::Rule::Account> > applies_to;
    vector<shared_ptr<MaskingRules::Rule::Account> > exempted;

    if (ok && pApplies_to)
    {
        ok = get_accounts(KEY_APPLIES_TO, pApplies_to, applies_to);
    }

    if (ok && pExempted)
    {
        ok = get_accounts(KEY_EXEMPTED, pExempted, exempted);
    }

    if (ok)
    {
        sRule = auto_ptr<MaskingRules::ReplaceRule>(new MaskingRules::ReplaceRule(column, table, database,
                                                                                  applies_to, exempted,
                                                                                  value, fill));
    }

    return sRule;
}

/**
 * Create a MaskingRules::Rule instance
 *
 * @param pReplace     A JSON object representing 'replace' of a rule from the rules file.
 * @param pWith        A JSON object representing 'with' of a rule from the rules file.
 * @param pApplies_to  A JSON object representing 'applies_to' of a rule from the rules file.
 * @param pExempted    A JSON object representing 'exempted' of a rule from the rules file.
 *
 * @return A Rule instance or NULL in case of error.
 */
auto_ptr<MaskingRules::ReplaceRule> create_rule_from_elements(json_t* pReplace,
                                                              json_t* pWith,
                                                              json_t* pApplies_to,
                                                              json_t* pExempted)
{
    ss_dassert(pReplace && json_is_object(pReplace));
    ss_dassert(pWith && json_is_object(pWith));
    ss_dassert(!pApplies_to || json_is_array(pApplies_to));
    ss_dassert(!pExempted || json_is_array(pExempted));

    auto_ptr<MaskingRules::ReplaceRule> sRule;

    json_t* pDatabase = json_object_get(pReplace, KEY_DATABASE);
    json_t* pTable = json_object_get(pReplace, KEY_TABLE);
    json_t* pColumn = json_object_get(pReplace, KEY_COLUMN);

    // A column is mandatory; both table and database are optional.
    if ((pColumn && json_is_string(pColumn)) &&
        (!pTable || json_is_string(pTable)) &&
        (!pDatabase || json_is_string(pDatabase)))
    {
        json_t* pValue = json_object_get(pWith, KEY_VALUE);
        json_t* pFill = json_object_get(pWith, KEY_FILL);

        if (!pFill)
        {
            // Allowed. Use default value for fill and add it to pWith.
            pFill = json_string("X");
            if (pFill)
            {
                json_object_set_new(pWith, KEY_FILL, pFill);
            }
            else
            {
                MXS_ERROR("json_string() error, cannot produce a valid rule.");
            }
        }
        if (pFill)
        {
            if ((!pValue || (json_is_string(pValue) && json_string_length(pValue))) &&
                (json_is_string(pFill) && json_string_length(pFill)))
            {
                sRule = create_rule_from_elements(pColumn, pTable, pDatabase,
                                                  pValue, pFill,
                                                  pApplies_to, pExempted);
            }
            else
            {
                MXS_ERROR("One of the keys '%s' or '%s' of masking rule object '%s' "
                          "has a non-string value or the string is empty.",
                          KEY_VALUE, KEY_FILL, KEY_WITH);
            }
        }
    }
    else
    {
        MXS_ERROR("The '%s' object of a masking rule does not have a '%s' key, or "
                  "the values of that key and/or possible '%s' and '%s' keys are "
                  "not strings.",
                  KEY_REPLACE, KEY_COLUMN, KEY_TABLE, KEY_DATABASE);
    }

    return sRule;
}

/**
 * Create all MaskingRules::Rule instances
 *
 * @param pRules  A JSON array representing 'rules' from the rules file.
 * @param rules   Vector where corresponding Rule instances will be pushed.
 *
 * @return True, if all rules could be created.
 */
bool create_rules_from_array(json_t* pRules, vector<shared_ptr<MaskingRules::Rule> >& rules)
{
    ss_dassert(json_is_array(pRules));

    bool parsed = true;

    size_t n = json_array_size(pRules);
    size_t i = 0;

    while (parsed && (i < n))
    {
        json_t* pRule = json_array_get(pRules, i);
        ss_dassert(pRule);

        if (json_is_object(pRule))
        {
            auto_ptr<MaskingRules::Rule> sRule = MaskingRules::ReplaceRule::create_from(pRule);

            if (sRule.get())
            {
                rules.push_back(shared_ptr<MaskingRules::Rule>(sRule.release()));
            }
            else
            {
                parsed = false;
            }
        }
        else
        {
            MXS_ERROR("Element %lu of the '%s' array is not an object.", i, KEY_RULES);
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
bool create_rules_from_root(json_t* pRoot, vector<shared_ptr<MaskingRules::Rule> >& rules)
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
            MXS_ERROR("The masking rules object contains a `%s` key, but it is not an array.", KEY_RULES);
        }
    }

    return parsed;
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

//
// MaskingRules::Rule
//

MaskingRules::Rule::Rule(const std::string& column,
                         const std::string& table,
                         const std::string& database,
                         const std::vector<SAccount>& applies_to,
                         const std::vector<SAccount>& exempted)
    : m_column(column)
    , m_table(table)
    , m_database(database)
    , m_applies_to(applies_to)
    , m_exempted(exempted)
{
}

MaskingRules::ReplaceRule::ReplaceRule(const std::string& column,
                                       const std::string& table,
                                       const std::string& database,
                                       const std::vector<SAccount>& applies_to,
                                       const std::vector<SAccount>& exempted,
                                       const std::string& value,
                                       const std::string& fill)
    : MaskingRules::Rule::Rule(column, table, database, applies_to, exempted)
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

//static
auto_ptr<MaskingRules::Rule> MaskingRules::ReplaceRule::create_from(json_t* pRule)
{
    ss_dassert(json_is_object(pRule));

    auto_ptr<MaskingRules::Rule> sRule;

    json_t* pReplace = json_object_get(pRule, KEY_REPLACE);
    json_t* pWith = json_object_get(pRule, KEY_WITH);
    json_t* pApplies_to = json_object_get(pRule, KEY_APPLIES_TO);
    json_t* pExempted = json_object_get(pRule, KEY_EXEMPTED);

    if (pReplace && pWith)
    {
        bool ok = true;

        if (!json_is_object(pReplace))
        {
            MXS_ERROR("A masking rule contains a '%s' key, but the value is not an object.",
                      KEY_REPLACE);
            ok = false;
        }

        if (!json_is_object(pWith))
        {
            MXS_ERROR("A masking rule contains a '%s' key, but the value is not an object.",
                      KEY_WITH);
            ok = false;
        }

        if (pApplies_to && !json_is_array(pApplies_to))
        {
            MXS_ERROR("A masking rule contains a '%s' key, but the value is not an array.",
                      KEY_APPLIES_TO);
            ok = false;
        }

        if (pExempted && !json_is_array(pExempted))
        {
            MXS_ERROR("A masking rule contains a '%s' key, but the value is not an array.",
                      KEY_EXEMPTED);
            ok = false;
        }

        if (ok)
        {
            sRule = create_rule_from_elements(pReplace, pWith, pApplies_to, pExempted);
        }
    }
    else
    {
        MXS_ERROR("A masking rule does not contain a '%s' and/or a '%s' key.", KEY_REPLACE, KEY_WITH);
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

namespace
{

class AccountMatcher : std::unary_function<MaskingRules::Rule::SAccount, bool>
{
public:
    AccountMatcher(const char* zUser, const char* zHost)
        : m_zUser(zUser)
        , m_zHost(zHost)
    {}

    bool operator()(const MaskingRules::Rule::SAccount& sAccount)
    {
        return sAccount->matches(m_zUser, m_zHost);
    }

private:
    const char* m_zUser;
    const char* m_zHost;
};

}

bool MaskingRules::Rule::matches(const ComQueryResponse::ColumnDef& column_def,
                                 const char* zUser,
                                 const char* zHost) const
{
    bool match =
        (m_column == column_def.org_name()) &&
        (m_table.empty() || (m_table == column_def.org_table())) &&
        (m_database.empty() || (m_database == column_def.schema()));

    if (match)
    {
        // If the column matched, then we need to check whether the rule applies
        // to the user and host.

        AccountMatcher matcher(zUser, zHost);

        if (m_applies_to.size() != 0)
        {
            match = false;

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
    }

    return match;
}

void MaskingRules::ReplaceRule::rewrite(LEncString& s) const
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
            LEncString::iterator i = s.begin();
            size_t len = m_fill.length();

            while (total_len)
            {
                if (total_len < len)
                {
                    len = total_len;
                }

                std::copy(m_fill.data(), m_fill.data() + len, i);

                i += len;
                total_len -= len;
            }
        }
        else
        {
            MXS_ERROR("Length of returned value \"%s\" is %u, while length of "
                      "replacement value \"%s\" is %u, and no 'fill' value specified.",
                      s.to_string().c_str(), (unsigned)s.length(),
                      m_value.c_str(), (unsigned)m_value.length());
        }
    }
}

//
// MaskingRules
//

MaskingRules::MaskingRules(json_t* pRoot, const std::vector<SRule>& rules)
    : m_pRoot(pRoot)
    , m_rules(rules)
{
    json_incref(m_pRoot);
}

MaskingRules::~MaskingRules()
{
    json_decref(m_pRoot);
}

//static
auto_ptr<MaskingRules> MaskingRules::load(const char* zPath)
{
    auto_ptr<MaskingRules> sRules;

    FILE* pFile = fopen(zPath, "r");

    if (pFile)
    {
        Closer<FILE*> file(pFile);

        json_error_t error;
        json_t* pRoot = json_loadf(file.get(), JSON_DISABLE_EOF_CHECK, &error);

        if (pRoot)
        {
            Closer<json_t*> root(pRoot);

            sRules = create_from(root.get());
        }
        else
        {
            MXS_ERROR("Loading rules file failed: (%s:%d:%d): %s",
                      zPath, error.line, error.column, error.text);
        }
    }
    else
    {
        MXS_ERROR("Could not open rules file %s for reading: %s",
                  zPath, mxs_strerror(errno));
    }

    return sRules;
}

//static
auto_ptr<MaskingRules> MaskingRules::parse(const char* zJson)
{
    auto_ptr<MaskingRules> sRules;

    json_error_t error;
    json_t* pRoot = json_loads(zJson, JSON_DISABLE_EOF_CHECK, &error);

    if (pRoot)
    {
        Closer<json_t*> root(pRoot);

        sRules = create_from(root.get());
    }
    else
    {
        MXS_ERROR("Parsing rules failed: (%d:%d): %s",
                  error.line, error.column, error.text);
    }

    return sRules;
}

//static
std::auto_ptr<MaskingRules> MaskingRules::create_from(json_t* pRoot)
{
    auto_ptr<MaskingRules> sRules;

    vector<SRule> rules;

    if (create_rules_from_root(pRoot, rules))
    {
        sRules = auto_ptr<MaskingRules>(new MaskingRules(pRoot, rules));
    }

    return sRules;
}

namespace
{

class RuleMatcher : std::unary_function<MaskingRules::SRule, bool>
{
public:
    RuleMatcher(const ComQueryResponse::ColumnDef& column_def,
                const char* zUser,
                const char* zHost)
        : m_column_def(column_def)
        , m_zUser(zUser)
        , m_zHost(zHost)
    {
    }

    bool operator()(const MaskingRules::SRule& sRule)
    {
        return sRule->matches(m_column_def, m_zUser, m_zHost);
    }

private:
    const ComQueryResponse::ColumnDef& m_column_def;
    const char* m_zUser;
    const char* m_zHost;
};

}

const MaskingRules::Rule* MaskingRules::get_rule_for(const ComQueryResponse::ColumnDef& column_def,
                                                     const char* zUser,
                                                     const char* zHost) const
{
    const Rule* pRule = NULL;

    RuleMatcher matcher(column_def, zUser, zHost);
    vector<SRule>::const_iterator i = std::find_if(m_rules.begin(), m_rules.end(), matcher);

    if (i != m_rules.end())
    {
        const SRule& sRule = *i;

        pRule = sRule.get();
    }

    return pRule;
}
