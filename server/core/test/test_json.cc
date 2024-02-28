/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-02-27
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/ccdefs.hh>

#include <cstring>
#include <string>
#include <iostream>

#include <maxbase/alloc.hh>
#include <maxbase/jansson.hh>
#include <maxbase/string.hh>
#include <maxscale/json_api.hh>

using std::string;

const char* test1_json =
    "{"
    "    \"links\": {"
    "        \"self\": \"http://localhost:8989/v1/servers/\""
    "    },"
    "    \"data\": ["
    "        {"
    "            \"id\": \"server1\","
    "            \"type\": \"servers\","
    "            \"relationships\": {"
    "                \"services\": {"
    "                    \"links\": {"
    "                        \"self\": \"http://localhost:8989/v1/services/\""
    "                    },"
    "                    \"data\": ["
    "                        {"
    "                            \"id\": \"RW-Split-Router\","
    "                            \"type\": \"services\""
    "                        },"
    "                        {"
    "                            \"id\": \"SchemaRouter-Router\","
    "                            \"type\": \"services\""
    "                        },"
    "                        {"
    "                            \"id\": \"RW-Split-Hint-Router\","
    "                            \"type\": \"services\""
    "                        },"
    "                        {"
    "                            \"id\": \"Read-Connection-Router\","
    "                            \"type\": \"services\""
    "                        }"
    "                    ]"
    "                },"
    "                \"monitors\": {"
    "                    \"links\": {"
    "                        \"self\": \"http://localhost:8989/v1/monitors/\""
    "                    },"
    "                    \"data\": ["
    "                        {"
    "                            \"id\": \"MySQL-Monitor\","
    "                            \"type\": \"monitors\""
    "                        }"
    "                    ]"
    "                }"
    "            },"
    "            \"attributes\": {"
    "                \"parameters\": {"
    "                    \"address\": \"127.0.0.1\","
    "                    \"port\": 3000,"
    "                    \"protocol\": \"MySQLBackend\""
    "                },"
    "                \"status\": \"Master, Running\","
    "                \"version_string\": \"10.1.19-MariaDB-1~jessie\","
    "                \"node_id\": 3000,"
    "                \"master_id\": -1,"
    "                \"replication_depth\": 0,"
    "                \"slaves\": ["
    "                    3001,"
    "                    3002,"
    "                    3003"
    "                ],"
    "                \"statistics\": {"
    "                    \"connections\": 0,"
    "                    \"total_connections\": 0,"
    "                    \"active_operations\": 0"
    "                }"
    "            }"
    "        },"
    "        {"
    "            \"id\": \"server2\","
    "            \"type\": \"servers\","
    "            \"relationships\": {"
    "                \"services\": {"
    "                    \"links\": {"
    "                        \"self\": \"http://localhost:8989/v1/services/\""
    "                    },"
    "                    \"data\": ["
    "                        {"
    "                            \"id\": \"RW-Split-Router\","
    "                            \"type\": \"services\""
    "                        },"
    "                        {"
    "                            \"id\": \"SchemaRouter-Router\","
    "                            \"type\": \"services\""
    "                        },"
    "                        {"
    "                            \"id\": \"RW-Split-Hint-Router\","
    "                            \"type\": \"services\""
    "                        }"
    "                    ]"
    "                },"
    "                \"monitors\": {"
    "                    \"links\": {"
    "                        \"self\": \"http://localhost:8989/v1/monitors/\""
    "                    },"
    "                    \"data\": ["
    "                        {"
    "                            \"id\": \"MySQL-Monitor\","
    "                            \"type\": \"monitors\""
    "                        }"
    "                    ]"
    "                }"
    "            },"
    "            \"attributes\": {"
    "                \"parameters\": {"
    "                    \"address\": \"127.0.0.1\","
    "                    \"port\": 3001,"
    "                    \"protocol\": \"MySQLBackend\""
    "                },"
    "                \"status\": \"Slave, Running\","
    "                \"version_string\": \"10.1.19-MariaDB-1~jessie\","
    "                \"node_id\": 3001,"
    "                \"master_id\": 3000,"
    "                \"replication_depth\": 1,"
    "                \"slaves\": [],"
    "                \"statistics\": {"
    "                    \"connections\": 0,"
    "                    \"total_connections\": 0,"
    "                    \"active_operations\": 0"
    "                }"
    "            }"
    "        },"
    "        {"
    "            \"id\": \"server3\","
    "            \"type\": \"servers\","
    "            \"relationships\": {"
    "                \"services\": {"
    "                    \"links\": {"
    "                        \"self\": \"http://localhost:8989/v1/services/\""
    "                    },"
    "                    \"data\": ["
    "                        {"
    "                            \"id\": \"RW-Split-Router\","
    "                            \"type\": \"services\""
    "                        },"
    "                        {"
    "                            \"id\": \"SchemaRouter-Router\","
    "                            \"type\": \"services\""
    "                        },"
    "                        {"
    "                            \"id\": \"RW-Split-Hint-Router\","
    "                            \"type\": \"services\""
    "                        }"
    "                    ]"
    "                },"
    "                \"monitors\": {"
    "                    \"links\": {"
    "                        \"self\": \"http://localhost:8989/v1/monitors/\""
    "                    },"
    "                    \"data\": ["
    "                        {"
    "                            \"id\": \"MySQL-Monitor\","
    "                            \"type\": \"monitors\""
    "                        }"
    "                    ]"
    "                }"
    "            },"
    "            \"attributes\": {"
    "                \"parameters\": {"
    "                    \"address\": \"127.0.0.1\","
    "                    \"port\": 3002,"
    "                    \"protocol\": \"MySQLBackend\""
    "                },"
    "                \"status\": \"Slave, Running\","
    "                \"version_string\": \"10.1.19-MariaDB-1~jessie\","
    "                \"node_id\": 3002,"
    "                \"master_id\": 3000,"
    "                \"replication_depth\": 1,"
    "                \"slaves\": [],"
    "                \"statistics\": {"
    "                    \"connections\": 0,"
    "                    \"total_connections\": 0,"
    "                    \"active_operations\": 0"
    "                }"
    "            }"
    "        },"
    "        {"
    "            \"id\": \"server4\","
    "            \"type\": \"servers\","
    "            \"relationships\": {"
    "                \"services\": {"
    "                    \"links\": {"
    "                        \"self\": \"http://localhost:8989/v1/services/\""
    "                    },"
    "                    \"data\": ["
    "                        {"
    "                            \"id\": \"RW-Split-Router\","
    "                            \"type\": \"services\""
    "                        },"
    "                        {"
    "                            \"id\": \"SchemaRouter-Router\","
    "                            \"type\": \"services\""
    "                        },"
    "                        {"
    "                            \"id\": \"RW-Split-Hint-Router\","
    "                            \"type\": \"services\""
    "                        }"
    "                    ]"
    "                },"
    "                \"monitors\": {"
    "                    \"links\": {"
    "                        \"self\": \"http://localhost:8989/v1/monitors/\""
    "                    },"
    "                    \"data\": ["
    "                        {"
    "                            \"id\": \"MySQL-Monitor\","
    "                            \"type\": \"monitors\""
    "                        }"
    "                    ]"
    "                }"
    "            },"
    "            \"attributes\": {"
    "                \"parameters\": {"
    "                    \"address\": \"127.0.0.1\","
    "                    \"port\": 3003,"
    "                    \"protocol\": \"MySQLBackend\""
    "                },"
    "                \"status\": \"Slave, Running\","
    "                \"version_string\": \"10.1.19-MariaDB-1~jessie\","
    "                \"node_id\": 3003,"
    "                \"master_id\": 3000,"
    "                \"replication_depth\": 1,"
    "                \"slaves\": [],"
    "                \"statistics\": {"
    "                    \"connections\": 0,"
    "                    \"total_connections\": 0,"
    "                    \"active_operations\": 0"
    "                }"
    "            }"
    "        }"
    "    ]"
    "}";

int test1()
{
    json_error_t err = {};
    json_t* json = json_loads(test1_json, 0, &err);

    mxb_assert(json);

    mxb_assert(mxb::json_ptr(json, "") == json);
    mxb_assert(mxb::json_ptr(json, "links") == json_object_get(json, "links"));
    mxb_assert(json_is_string(mxb::json_ptr(json, "links/self")));

    mxb_assert(mxb::json_ptr(json, "data") == json_object_get(json, "data"));
    mxb_assert(json_is_array(mxb::json_ptr(json, "data")));

    mxb_assert(json_is_object(mxb::json_ptr(json, "data/0")));
    mxb_assert(json_is_string(mxb::json_ptr(json, "data/0/id")));
    string s = json_string_value(mxb::json_ptr(json, "data/0/id"));
    mxb_assert(s == "server1");

    mxb_assert(json_is_object(mxb::json_ptr(json, "data/1")));
    mxb_assert(json_is_string(mxb::json_ptr(json, "data/1/id")));
    s = json_string_value(mxb::json_ptr(json, "data/1/id"));
    mxb_assert(s == "server2");

    mxb_assert(json_is_object(mxb::json_ptr(json, "data/0/attributes")));
    mxb_assert(json_is_object(mxb::json_ptr(json, "data/0/attributes/parameters")));
    mxb_assert(json_is_integer(mxb::json_ptr(json, "data/0/attributes/parameters/port")));
    int i = json_integer_value(mxb::json_ptr(json, "data/0/attributes/parameters/port"));
    mxb_assert(i == 3000);

    mxb_assert(json_is_array(mxb::json_ptr(json, "data/0/attributes/slaves")));
    mxb_assert(json_array_size(mxb::json_ptr(json, "data/0/attributes/slaves")) == 3);

    json_decref(json);

    return 0;
}

int test2()
{
    char* s;
    json_t* err;

    err = mxs_json_error("%s", "This is an error!");
    s = json_dumps(err, 0);
    printf("%s\n", s);
    mxb_assert(strcmp(s, "{\"errors\": [{\"detail\": \"This is an error!\"}]}") == 0);
    MXB_FREE(s);

    json_decref(err);

    err = mxs_json_error_append(NULL, "%s", "This is an error!");
    s = json_dumps(err, 0);
    printf("%s\n", s);
    mxb_assert(strcmp(s, "{\"errors\": [{\"detail\": \"This is an error!\"}]}") == 0);
    MXB_FREE(s);

    err = mxs_json_error_append(err, "%s", "This is another error!");
    s = json_dumps(err, 0);
    printf("%s\n", s);
    mxb_assert(strcmp(s,
                      "{\"errors\": [{\"detail\": \"This is an error!\"}, "
                      "{\"detail\": \"This is another error!\"}]}") == 0);
    MXB_FREE(s);

    json_decref(err);

    return 0;
}

int compare(std::vector<mxb::Json> result, std::vector<mxb::Json> expected)
{
    auto to_str = [](const mxb::Json& json) {
        return json.to_string(mxb::Json::Format::COMPACT);
    };

    int errors = 0;
    auto str_result = mxb::transform_join(result, to_str);
    auto str_expected = mxb::transform_join(expected, to_str);

    if (str_expected != str_result)
    {
        std::cout << "Error: " << str_result << " != " << str_expected << std::endl;
        ++errors;
    }

    return errors;
}

int test_json_path()
{
    // From the author of JsonPath: https://goessner.net/articles/JsonPath/
    std::string raw_json =
        R"(
{ "store": {
    "book": [
      { "category": "reference",
        "author": "Nigel Rees",
        "title": "Sayings of the Century",
        "price": 8.95
      },
      { "category": "fiction",
        "author": "Evelyn Waugh",
        "title": "Sword of Honour",
        "price": 12.99
      },
      { "category": "fiction",
        "author": "Herman Melville",
        "title": "Moby Dick",
        "isbn": "0-553-21311-3",
        "price": 8.99
      },
      { "category": "fiction",
        "author": "J. R. R. Tolkien",
        "title": "The Lord of the Rings",
        "isbn": "0-395-19395-8",
        "price": 22.99
      }
    ],
    "bicycle": {
      "color": "red",
      "price": 19.95
    }
  }
})";

    mxb::Json js;
    js.load_string(raw_json);

    // Used to store JSON results
    std::vector<mxb::Json> result;
    auto store_objects = [&](json_t* json){
        result.push_back(mxb::Json(json, mxb::Json::RefType::COPY));
    };

    int errors = 0;
    auto test_one = [&](const char* path, std::vector<mxb::Json> expected) {
        result.clear();
        mxb::json_path(js.get_json(), path, store_objects);
        int err = compare(result, expected);

        // Test without the root object as well.
        if (path[0] == '$' && path[1] == '.' && path[2] != '\0')
        {
            result.clear();
            mxb::json_path(js.get_json(), path + 2, store_objects);
            err += compare(result, expected);
        }

        errors += err;

        if (err)
        {
            std::cout << "Path: " << path << std::endl;
        }
    };

    // Root object
    test_one("$", {js});

    // Object
    test_one("$.store", {js.at("store")});

    // Sub-object
    test_one("$.store.bicycle", {js.at("store/bicycle")});

    // Field of a sub-object
    test_one("$.store.bicycle.color", {js.at("store/bicycle/color")});

    // Bracket notation
    test_one("$['store']['bicycle']['color']", {js.at("store/bicycle/color")});

    // Bracket and dot notation
    test_one("$['store'].bicycle['color']", {js.at("store/bicycle/color")});

    // Array
    test_one("$.store.book", {js.at("store/book")});

    // Array value
    test_one("$.store.book[1]", {js.at("store/book/1")});

    // Wildcard that matches multiple array values
    test_one("$.store.book[*].author", {
        js.at("store/book/0/author"),
        js.at("store/book/1/author"),
        js.at("store/book/2/author"),
        js.at("store/book/3/author")
    });

    // Wildcard that matches multiple array values
    test_one("$.store.bicycle.*", {
        js.at("store/bicycle/color"),
        js.at("store/bicycle/price")
    });

    // Wildcard that matches multiple array values
    test_one("$.store.*.color", {
        js.at("store/bicycle/color")
    });

    // Multiple array values
    test_one("$.store.book[1,2].author", {
        js.at("store/book/1/author"),
        js.at("store/book/2/author")
    });

    // Array values in specified order
    test_one("$.store.book[2,0,3,1].price", {
        js.at("store/book/2/price"),
        js.at("store/book/0/price"),
        js.at("store/book/3/price"),
        js.at("store/book/1/price"),
    });

    // Wrong paths do not generate output
    test_one("", {});
    test_one("store.", {});
    test_one(".", {});
    test_one("$.", {});
    test_one("store/book", {});
    test_one("sto.re", {});
    test_one("ಠ_ಠ", {});
    test_one("🍣🍺", {});

    return errors;
}

int main(int argc, char** argv)
{
    int errors = 0;
    errors += test1();
    errors += test2();
    errors += test_json_path();
    return errors;
}
