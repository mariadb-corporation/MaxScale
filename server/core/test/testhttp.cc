/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2019-07-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "../maxscale/httprequest.hh"
#include "../maxscale/httpresponse.hh"

#include <sstream>

using std::stringstream;
using std::string;

#define TEST(a, b, ...) do{if (!(a)){printf(b "\n", ##__VA_ARGS__);return 1;}}while(false)

const char* verbs_pass[] =
{
    "GET",
    "PUT",
    "POST",
    "OPTIONS",
    "PATCH",
    "HEAD",
    NULL
};

const char* verbs_fail[] =
{
    "LOAD",
    "STORE",
    "PUBLISH",
    "Something that's not a verb",
    "⬠",
    NULL
};

static struct
{
    const char* input;
    const char* output;
} paths_pass[] =
{
    { "/", "/" },
    { "*", "*" },
    { "/test/", "/test/" },
    { "/test", "/test" },
    { "/servers/list", "/servers/list" },
    { "/servers/list/", "/servers/list/" },
    { "/?test=true", "/" },
    { "/test/?test=y", "/test/" },
    { "/?", "/" },
    {}
};

const char* paths_fail[] =
{
    "-strikethrough-",
    "_underline_",
    "*bold*",
    "?",
    NULL
};

const char* proto_pass[] =
{
    "HTTP/1.1",
    NULL
};

const char* proto_fail[] =
{
    "HTTP/2.0",
    "SMTP/0.0",
    "CDC/1.0",
    NULL
};

int test_basic()
{
    /** Test parts that should pass */
    for (int i = 0; verbs_pass[i]; i++)
    {
        for (int j = 0; paths_pass[j].input; j++)
        {
            for (int k = 0; proto_pass[k]; k++)
            {
                stringstream ss;
                ss << verbs_pass[i] << " " << paths_pass[j].input << " " << proto_pass[k] << "\r\n\r\n";
                SHttpRequest parser(HttpRequest::parse(ss.str()));
                TEST(parser.get() != NULL, "Valid HTTP request should be parsed: %s", ss.str().c_str());
                TEST(parser->get_resource() == string(paths_pass[j].output),
                     "The request path '%s' should be correct: %s",
                     paths_pass[j].output, parser->get_resource().c_str());
            }
        }
    }

    /** Test parts that should fail */
    for (int i = 0; verbs_fail[i]; i++)
    {
        for (int j = 0; paths_fail[j]; j++)
        {
            for (int k = 0; proto_fail[k]; k++)
            {
                stringstream ss;
                ss << verbs_fail[i] << " " << paths_fail[j] << " " << proto_fail[k] << "\r\n\r\n";
                SHttpRequest parser(HttpRequest::parse(ss.str()));
                TEST(parser.get() == NULL, "Invalid HTTP request should not be parsed: %s", ss.str().c_str());
            }
        }
    }

    return 0;
}

static struct
{
    const char* key;
    const char* value;
} headers_pass[] =
{
    {"Accept", "*/*"},
    {"User-Agent", "curl/7.51.0"},
    {"Authorization", "bWF4dXNlcjptYXhwd2QK"},
    {"Content-Type", "application/json"},
    {"Date", "1.1.2017 10:10:10"},
    {"Host", "127.0.0.1:8080"},
    {"If-Match", "bWF4dXNlcjptYXhwd2QK"},
    {"If-Modified-Since", "Mon, 18 Nov 2013 08:14:29 -0600"},
    {"If-None-Match", "bWF4dXNlcjptYXhwd2QK"},
    {"If-Unmodified-Since", "Mon, 18 Nov 2013 08:14:29 -0600"},
    {"X-HTTP-Method-Override", "PATCH"},
    {"Allow", "GET, PATCH, PUT"},
    {"Accept-Patch", "application/json-patch"},
    {"Date", "Mon, 18 Nov 2013 08:14:29 -0600"},
    {"ETag", "bWF4dXNlcjptYXhwd2QK"},
    {"Last-Modified", "Mon, 18 Nov 2013 08:14:29 -0600"},
    {"Location", "/servers/server1"},
    {"WWW-Authenticate", "Basic"},
    {0}
};

int test_headers()
{
    for (int i = 0; headers_pass[i].key; i++)
    {
        stringstream ss;
        ss <<  "GET / HTTP/1.1\r\n" << headers_pass[i].key << ": "
           << headers_pass[i].value << "\r\n\r\n";
        SHttpRequest parser(HttpRequest::parse(ss.str()));
        TEST(parser.get() != NULL, "Valid HTTP request should be parsed: %s", ss.str().c_str());
        TEST(parser->get_header(headers_pass[i].key).length() > 0, "Header should be found");
        TEST(parser->get_header(headers_pass[i].key) == string(headers_pass[i].value),
             "Header value should be correct: %s", parser->get_header(headers_pass[i].key).c_str());
    }

    return 0;
}

/**
 The following JSON tests are imported from the Jansson test suite
 */

const char* body_pass[] =
{
    "{\"i\": [1]}",
    "{\"i\": [1.8011670033376514e-308]}",
    "{\"i\": [123.456e78]}",
    "{\"i\": [-1]}",
    "{\"i\": [-123]}",
    "{\"i\": [\"\u0821 three-byte UTF-8\"]}",
    "{\"i\": [123]}",
    "{\"i\": [1E+2]}",
    "{\"i\": [123e45]}",
    "{\"i\": [false]}",
    "{\"i\": [\"\u002c one-byte UTF-8\"]}",
    "{\"i\": {\"a\":[]}}",
    "{\"i\": [\"abcdefghijklmnopqrstuvwxyz1234567890 \"]}",
    "{\"i\": [-0]}",
    "{\"i\": [\"\"]}",
    "{\"i\": [1,2,3,4]}",
    "{\"i\": [\"a\", \"b\", \"c\"]}",
    "{\"foo\": \"bar\", \"core\": \"dump\"}",
    "{\"i\": [true, false, true, true, null, false]}",
    "{\"b\": [\"a\"]}",
    "{\"i\": [true]}",
    "{\"i\": {}}",
    "{\"i\": [{}]}",
    "{\"i\": [0]}",
    "{\"i\": [123.456789]}",
    "{\"i\": [1e+2]}",
    "{\"i\": [\"\u0123 two-byte UTF-8\"]}",
    "{\"i\": [123e-10000000]}",
    "{\"i\": [null]}",
    "{\"i\": [\"€þıœəßð some utf-8 ĸʒ×ŋµåäö𝄞\"]}",
    "{\"i\": [1e-2]}",
    "{\"i\": [1E22]}",
    "{\"i\": [1E-2]}",
    "{\"i\": []}",

    /** Additional tests */
    "{\"this is\": \"a JSON value\"}",
    NULL
};

const char* body_fail[] =
{
    "{{}",
    "{[-123foo]}",
    "{[1,}",
    "{[troo}",
    "{{\"a\"}",
    "{[-123123123123123123123123123123]}",
    "{{[}",
    "{[1.]}",
    "{[1ea]}",
    "{['}",
    "{[-012]}",
    "{[012]}",
    "{{\"a}",
    "{[{}",
    "{[123123123123123123123123123123]}",
    "{[1,2,3]}",
    "{foo}",
    "{[\"\a <-- invalid escape\"]}",
    "{[{}}",
    "{[\"	 <-- tab character\"]}",
    "{[\"a\"}",
    "{{'a'}",
    "{[,}",
    "{{\"a\":}",
    "{{\"a\":\"a}",
    "{[-123123e100000]}",
    "{[\"null escape \u0000 not allowed\"]}",
    "{[1,}",
    "{2,}",
    "{3,}",
    "{4,}",
    "{5,}",
    "{]}",
    "{null}",
    "{[-123.123foo]}",
    "{[}",
    "{aå}",
    "{{\"foo\u0000bar\": 42}{\"a\":\"a\" 123}}",
    "{[\"a}",
    "{[123123e100000]}",
    "{[1e]}",
    "{[1,]}",
    "{{,}",
    "{[-foo]}",
    "{å}",
    "{{\"}",
    "{[\"null byte  not allowed\"]}",
    "{[}",
    "{[1,2,3]foo}",

    /** Additional tests */
    "Hello World!",
    "<p>I am a paragraph</p>",
    "",
    NULL
};

const char* body_verbs_pass[] =
{
    "PUT",
    "POST",
    "PATCH",
    NULL
};

int test_message_body()
{
    for (int i = 0; body_pass[i]; i++)
    {
        for (int j = 0; body_verbs_pass[j]; j++)
        {
            /** Only PUT/POST/PATCH methods should have request bodies */
            stringstream ss;
            ss << body_verbs_pass[j] << " / HTTP/1.1\r\n\r\n" << body_pass[i];
            SHttpRequest parser(HttpRequest::parse(ss.str()));
            TEST(parser.get() != NULL, "Valid request body should be parsed: %s",
                 ss.str().c_str());
            TEST(parser->get_json(), "Body should be found");
            TEST(parser->get_json_str() == body_pass[i], "Body value should be correct: %s",
                 parser->get_json_str().c_str());
        }
    }

    for (int i = 0; body_pass[i]; i++)
    {
        for (int j = 0; verbs_pass[j]; j++)
        {
            stringstream ss;
            ss << verbs_pass[j] << " / HTTP/1.1\r\n\r\n" << body_fail[i];
            SHttpRequest parser(HttpRequest::parse(ss.str()));
            TEST(parser.get() == NULL, "Invalid request body should not be parsed: %s",
                 ss.str().c_str());
        }
    }

    return 0;
}

static struct
{
    const char* input;
    const char* key;
    const char* value;
} options_pass[] =
{
    { "/", "", "" },
    { "*", "", "" },
    { "/?a=b", "a", "b" },
    { "/?a=b,c=d", "a", "b" },
    { "/?a=b,c=d", "c", "d" },
    { "/test?q=w", "q", "w" },
    { "/servers/list?all=false", "all", "false" },
    { "/servers/list/?pretty=true", "pretty", "true"},
    { "/?test=true", "test", "true" },
    { "/test/?test=y", "test", "y" },
    { "/?", "", "" },
    {}
};

const char* options_fail[] =
{
    "/?,",
    "/??",
    "/test?/",
    "/test/?a,b",
    "/test?a,",
    NULL
};

int test_options()
{
    for (int i = 0; options_pass[i].input; i++)
    {
        stringstream ss;
        ss << "GET " << options_pass[i].input << " HTTP/1.1\r\n\r\n";
        SHttpRequest parser(HttpRequest::parse(ss.str()));

        TEST(parser.get() != NULL, "Valid option should be parsed: %s", ss.str().c_str());
        TEST(parser->get_option(options_pass[i].key) == options_pass[i].value,
             "The option value for '%s' should be '%s': %s",
             options_pass[i].key, options_pass[i].value,
             parser->get_option(options_pass[i].key).c_str());
    }

    for (int i = 0; options_fail[i]; i++)
    {
        stringstream ss;
        ss << "GET " << options_fail[i] << " HTTP/1.1\r\n\r\n";
        SHttpRequest parser(HttpRequest::parse(ss.str()));
        TEST(parser.get() == NULL, "Invalid option should not be parsed: %s", ss.str().c_str());
    }

    return 0;
}

int test_response()
{
    TEST(HttpResponse().get_response().find("200 OK") != string::npos,
         "Default constructor should return a 200 OK with no body");
    TEST(HttpResponse("Test").get_response().find("\r\n\r\nTest") != string::npos,
         "Custom payload should be found in the response");
    TEST(HttpResponse("", HTTP_204_NO_CONTENT).get_response().find("204 No Content") != string::npos,
         "Using custom header should generate correct response");

    HttpResponse response("A Bad gateway", HTTP_502_BAD_GATEWAY);
    TEST(response.get_response().find("\r\n\r\nA Bad gateway") != string::npos &&
         response.get_response().find("502 Bad Gateway") != string::npos,
         "Both custom response body and return code should be found");

    return 0;
}

int main(int argc, char** argv)
{
    int rc = 0;

    rc += test_basic();
    rc += test_headers();
    rc += test_message_body();
    rc += test_response();

    return rc;
}
