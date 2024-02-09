/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-01-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "internal/httpresponse.hh"

#include <string>
#include <sstream>
#include <sys/time.h>

#include <maxscale/cn_strings.hh>
#include <maxscale/json_api.hh>

#include "internal/admin.hh"

using std::string;
using std::stringstream;

namespace
{

using ParseError = std::runtime_error;

class Matcher
{
public:
    virtual ~Matcher() = default;
    virtual bool match(const mxb::Json& js) const = 0;
};

template<auto func>
class ComparisonMatcher : public Matcher
{
public:
    ComparisonMatcher(mxb::Json json)
        : m_json(std::move(json))
    {
    }

    bool match(const mxb::Json& js) const override
    {
        return func(js, m_json);
    }

private:
    mxb::Json m_json;
};

class LogicMatcher : public Matcher
{
public:
    using Expressions = std::vector<std::unique_ptr<Matcher>>;

    LogicMatcher(Expressions expr)
        : m_expr(std::move(expr))
    {
    }

protected:
    Expressions m_expr;
};

class AndMatcher : public LogicMatcher
{
public:
    using LogicMatcher::LogicMatcher;

    bool match(const mxb::Json& js) const override
    {
        return std::all_of(m_expr.begin(), m_expr.end(), [&](const auto& e){
            return e->match(js);
        });
    }
};

class OrMatcher : public LogicMatcher
{
public:
    using LogicMatcher::LogicMatcher;

    bool match(const mxb::Json& js) const override
    {
        return std::any_of(m_expr.begin(), m_expr.end(), [&](const auto& e){
            return e->match(js);
        });
    }
};

class NotMatcher : public LogicMatcher
{
public:
    using LogicMatcher::LogicMatcher;

    bool match(const mxb::Json& js) const override
    {
        return std::none_of(m_expr.begin(), m_expr.end(), [&](const auto& e){
            return e->match(js);
        });
    }
};

bool eq_json(const mxb::Json& lhs, const mxb::Json& rhs)
{
    return lhs == rhs;
}

bool ne_json(const mxb::Json& lhs, const mxb::Json& rhs)
{
    return !eq_json(lhs, rhs);
}

bool lt_json(const mxb::Json& lhs, const mxb::Json& rhs)
{
    if (lhs.type() == rhs.type())
    {
        switch (lhs.type())
        {
        case mxb::Json::Type::STRING:
            return lhs.get_string() < rhs.get_string();

        case mxb::Json::Type::INTEGER:
            return lhs.get_int() < rhs.get_int();

        case mxb::Json::Type::REAL:
            return lhs.get_real() < rhs.get_real();

        default:
            return false;
        }
    }

    return false;
}

bool le_json(const mxb::Json& lhs, const mxb::Json& rhs)
{
    return lt_json(lhs, rhs) || !lt_json(rhs, lhs);
}

bool gt_json(const mxb::Json& lhs, const mxb::Json& rhs)
{
    return lt_json(rhs, lhs);
}

bool ge_json(const mxb::Json& lhs, const mxb::Json& rhs)
{
    return lt_json(rhs, lhs) || !lt_json(lhs, rhs);
}

class MatcherParser
{
public:
    MatcherParser(const std::string& str)
        : m_str(str)
    {
    }

    std::unique_ptr<Matcher> parse()
    {
        try
        {
            auto rval = parse_expr();

            if (!m_str.empty())
            {
                throw ParseError(mxb::cat("Unexpected trailing data: ", m_str));
            }

            return rval;
        }
        catch (const ParseError& e)
        {
            MXB_ERROR("%s", e.what());
            return nullptr;
        }
    }

private:
    std::unique_ptr<Matcher> parse_expr()
    {
        if (m_str.empty())
        {
            throw ParseError("Empty filter expression");
        }

        if (try_consume("eq"))
        {
            return make_comparison<eq_json>();
        }
        else if (try_consume("ne"))
        {
            return make_comparison<ne_json>();
        }
        else if (try_consume("lt"))
        {
            return make_comparison<lt_json>();
        }
        else if (try_consume("gt"))
        {
            return make_comparison<gt_json>();
        }
        else if (try_consume("le"))
        {
            return make_comparison<le_json>();
        }
        else if (try_consume("ge"))
        {
            return make_comparison<ge_json>();
        }
        else if (try_consume("and"))
        {
            return make_logic<AndMatcher>();
        }
        else if (try_consume("or"))
        {
            return make_logic<OrMatcher>();
        }
        else if (try_consume("not"))
        {
            return make_logic<NotMatcher>();
        }

        throw ParseError(mxb::cat("Not a valid filter expression: ", m_str));
    }

    /**
     * Constructs a comparison element in the parsed match expression
     *
     * @tparam func The filtering function
     *
     * @return A new Matcher that uses the given function for matching to the JSON element passed to it
     */
    template<auto func>
    std::unique_ptr<Matcher> make_comparison()
    {
        consume("(");
        auto json = consume_json();
        consume(")");
        return std::make_unique<ComparisonMatcher<func>>(std::move(json));
    }

    /**
     * Constructs a logical operator element in the parsed match expression
     *
     * The expression must be a non-empty comma separated list of sub-expressions.
     *
     * @tparam func The LogicMatcher type
     *
     * @return A new conjunction Matcher
     */
    template<class Type>
    std::unique_ptr<Matcher> make_logic()
    {
        std::vector<std::unique_ptr<Matcher>> expr;
        consume("(");

        do
        {
            expr.push_back(parse_expr());
        }
        while (try_consume(","));

        consume(")");
        return std::make_unique<Type>(std::move(expr));
    }

    mxb::Json consume_json()
    {
        json_error_t err;
        json_t* js = json_loadb(m_str.data(), m_str.size(), JSON_DECODE_ANY | JSON_DISABLE_EOF_CHECK, &err);

        if (!js)
        {
            throw ParseError(mxb::cat("Invalid JSON: ", m_str));
        }

        m_str = m_str.substr(err.position);
        return mxb::Json(js, mxb::Json::RefType::STEAL);
    }


    bool try_consume(std::string_view expected)
    {
        bool consumed = m_str.substr(0, expected.size()) == expected;

        if (consumed)
        {
            m_str.remove_prefix(expected.size());
        }

        return consumed;
    }

    void consume(std::string_view expected)
    {
        auto tok = m_str.substr(0, expected.size());

        if (tok != expected)
        {
            throw ParseError(mxb::cat("Expected '", expected, "', got '", tok, "'"));
        }

        m_str.remove_prefix(tok.size());
    }

    std::string_view m_str;
};

template<class Compare>
void filter_body(json_t* body, const std::string& json_ptr, Compare comp)
{
    if (json_t* data = json_object_get(body, CN_DATA))
    {
        if (json_is_array(data))
        {
            json_t* val;
            size_t i;
            json_t* new_arr = json_array();

            json_array_foreach(data, i, val)
            {
                if (json_t* lhs = mxb::json_ptr(val, json_ptr.c_str()); lhs && comp(lhs))
                {
                    json_array_append_new(new_arr, json_copy(val));
                }
            }

            json_object_set_new(body, CN_DATA, new_arr);
        }
    }
}
}

HttpResponse::HttpResponse(int code, json_t* response)
    : m_body(response)
    , m_code(code)
    , m_headers{{HTTP_RESPONSE_HEADER_DATE, http_get_date()}}
{
    if (m_body)
    {
        add_header(HTTP_RESPONSE_HEADER_CONTENT_TYPE, "application/json");
    }
}

HttpResponse::HttpResponse(Handler handler)
    : HttpResponse(MHD_HTTP_SWITCHING_PROTOCOLS)
{
    m_handler = handler;
}

HttpResponse::HttpResponse(Callback callback)
    : HttpResponse(MHD_HTTP_BAD_REQUEST)
{
    m_cb = callback;
}

HttpResponse::HttpResponse(const HttpResponse& response)
    : m_body(json_incref(response.m_body))
    , m_code(response.m_code)
    , m_headers(response.m_headers)
    , m_handler(response.m_handler)
    , m_cb(response.m_cb)
    , m_cookies(response.m_cookies)
{
}

HttpResponse& HttpResponse::operator=(const HttpResponse& response)
{
    json_t* body = m_body;
    m_body = json_incref(response.m_body);
    m_code = response.m_code;
    m_headers = response.m_headers;
    m_handler = response.m_handler;
    m_cb = response.m_cb;
    m_cookies = response.m_cookies;
    json_decref(body);
    return *this;
}

HttpResponse::~HttpResponse()
{
    if (m_body)
    {
        json_decref(m_body);
    }
}

json_t* HttpResponse::get_response() const
{
    return m_body;
}

void HttpResponse::drop_response()
{
    json_decref(m_body);
    m_body = NULL;
}

int HttpResponse::get_code() const
{
    return m_code;
}

void HttpResponse::add_header(const string& key, const string& value)
{
    m_headers[key] = value;
}

const HttpResponse::Headers& HttpResponse::get_headers() const
{
    return m_headers;
}

void HttpResponse::remove_fields_from_object(json_t* obj, std::vector<std::string>&& fields)
{
    if (fields.empty())
    {
        return;
    }

    if (json_is_object(obj))
    {
        if (json_t* p = json_object_get(obj, fields.front().c_str()))
        {
            // Remove all other keys
            json_incref(p);
            json_object_clear(obj);
            json_object_set_new(obj, fields.front().c_str(), p);

            fields.erase(fields.begin());
            remove_fields_from_object(p, std::move(fields));
        }
        else
        {
            json_object_clear(obj);
        }
    }
    else
    {
        json_object_clear(obj);
    }
}

void HttpResponse::remove_fields_from_resource(json_t* obj, const std::string& type,
                                               const std::unordered_set<std::string>& fields)
{
    json_t* t = json_object_get(obj, CN_TYPE);

    if (json_is_string(t) && json_string_value(t) == type)
    {
        if (json_t* attr = json_object_get(obj, CN_ATTRIBUTES))
        {
            json_t* newattr = json_object();

            for (const auto& a : fields)
            {
                json_t* tmp = json_deep_copy(attr);
                remove_fields_from_object(tmp, mxb::strtok(a, "/"));
                json_object_update_recursive(newattr, tmp);
                json_decref(tmp);
            }

            json_object_set_new(obj, CN_ATTRIBUTES, newattr);

            if (json_object_size(newattr) == 0)
            {
                json_object_del(obj, CN_ATTRIBUTES);
            }
        }

        if (json_t* rel = json_object_get(obj, CN_RELATIONSHIPS))
        {
            json_t* newrel = json_object();

            for (const auto& a : fields)
            {
                json_t* tmp = json_deep_copy(rel);
                remove_fields_from_object(tmp, mxb::strtok(a, "/"));
                json_object_update_recursive(newrel, tmp);
                json_decref(tmp);
            }

            json_object_set_new(obj, CN_RELATIONSHIPS, newrel);

            if (json_object_size(newrel) == 0)
            {
                json_object_del(obj, CN_RELATIONSHIPS);
            }
        }
    }
}

void HttpResponse::add_cookie(const std::string& name, const std::string& token, uint32_t max_age)
{
    std::string cookie_opts = "; Path=/";

    if (max_age)
    {
        cookie_opts += "; Max-Age=" + std::to_string(max_age);
    }

    set_cookie(name, token, cookie_opts);
}

void HttpResponse::remove_cookie(const std::string& name)
{
    set_cookie(name, "", "; Path=/; Expires=" + http_to_date(0));
}

void HttpResponse::set_cookie(const std::string& name,
                              const std::string& token,
                              const std::string& cookie_opts)
{
    const bool cors = mxs_admin_use_cors();
    std::string secure_opt = mxs_admin_https_enabled() || cors ? "; Secure" : "";
    std::string priv_opts = "; SameSite=Strict; HttpOnly";

    if (cors)
    {
        priv_opts = "; SameSite=None; HttpOnly";
    }

    add_cookie(name + "=" + token + cookie_opts + secure_opt + priv_opts);
}

void HttpResponse::remove_fields(const std::string& type, const std::unordered_set<std::string>& fields)
{
    if (json_t* data = json_object_get(m_body, CN_DATA))
    {
        if (json_is_array(data))
        {
            json_t* val;
            size_t i;

            json_array_foreach(data, i, val)
            {
                remove_fields_from_resource(val, type, fields);
            }
        }
        else
        {
            remove_fields_from_resource(data, type, fields);
        }
    }
}

bool HttpResponse::remove_rows(const std::string& json_ptr, const std::string& value)
{
    bool ok = true;
    json_error_t err;

    if (json_t* js = json_loads(value.c_str(), JSON_DECODE_ANY, &err))
    {
        // Legacy filtering, compares equality to JSON
        filter_body(m_body, json_ptr, [&](json_t* lhs){
            return json_equal(lhs, js);
        });
        json_decref(js);
    }
    else if (auto matcher = MatcherParser(value).parse())
    {
        // Filtering expression
        filter_body(m_body, json_ptr, [&](json_t* lhs){
            return matcher->match(mxb::Json(lhs, mxb::Json::RefType::COPY));
        });
    }
    else
    {
        ok = false;
    }

    return ok;
}

void HttpResponse::paginate(int64_t limit, int64_t offset)
{
    mxb_assert(limit > 0);

    if (json_t* data = json_object_get(m_body, CN_DATA))
    {
        if (json_is_array(data))
        {
            int64_t total_size = json_array_size(data);

            // Don't actually paginate the data if only one page would be generated
            if (total_size > limit)
            {
                json_t* new_data = json_array();

                for (int64_t i = offset * limit; i < (offset + 1) * limit; i++)
                {
                    if (json_t* v = json_array_get(data, i))
                    {
                        json_array_append(new_data, v);
                    }
                }

                json_object_set_new(m_body, CN_DATA, new_data);
            }

            // Create pagination links only if the resource itself didn't create them. The /maxscale/logs/data
            // endpoint has its own pagination links and they must not be overwritten.
            json_t* links = json_object_get(m_body, CN_LINKS);

            if (links && !json_object_get(links, "next") && !json_object_get(links, "prev")
                && !json_object_get(links, "last") && !json_object_get(links, "first"))
            {
                mxb_assert(json_object_get(links, "self"));
                const std::string LB = "%5B";   // Percent-encoded [
                const std::string RB = "%5D";   // Percent-encoded ]

                std::string base = json_string_value(json_object_get(links, "self"));
                base += "?page" + LB + "size" + RB + "=" + std::to_string(limit)
                    + "&page" + LB + "number" + RB + "=";

                // Generate the paginated self link
                auto self = base + std::to_string(offset);
                json_object_set_new(links, "self", json_string(self.c_str()));

                if ((offset + 1) * limit < total_size)
                {
                    // More pages available
                    auto next = base + std::to_string(offset + 1);
                    json_object_set_new(links, "next", json_string(next.c_str()));
                }

                auto first = base + std::to_string(0);
                json_object_set_new(links, "first", json_string(first.c_str()));

                // The same calculation that is used to count how many bytes are needed for N bits can be used
                // to determine how many pages we have: (bits + 7) / 8 = bytes
                // Pages are indexed from 0 so we decrement the value by one.
                auto last = base + std::to_string(((total_size + (limit - 1)) / limit) - 1);
                json_object_set_new(links, "last", json_string(last.c_str()));

                if (offset > 0 && offset * limit < total_size)
                {
                    auto prev = base + std::to_string(offset - 1);
                    json_object_set_new(links, "prev", json_string(prev.c_str()));
                }
            }

            json_t* meta = json_object_get(m_body, "meta");

            if (!meta)
            {
                json_object_set_new(m_body, "meta", json_object());
                meta = json_object_get(m_body, "meta");
            }

            json_object_set_new(meta, "total", json_integer(total_size));
        }
    }
}

std::string HttpResponse::to_string() const
{
    std::ostringstream ss;
    ss << "HTTP " << m_code << "\n";

    for (const auto& [key, value] : m_headers)
    {
        ss << key << ": " << value << "\n";
    }

    if (m_body)
    {
        ss << mxb::json_dump(m_body, JSON_INDENT(2));
    }

    return ss.str();
}
