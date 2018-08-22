/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2022-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/ccdefs.hh>

#include <string>

#include <maxscale/debug.h>
#include <maxscale/jansson.hh>
#include <maxscale/json_api.h>

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

    mxb_assert(mxs_json_pointer(json, "") == json);
    mxb_assert(mxs_json_pointer(json, "links") == json_object_get(json, "links"));
    mxb_assert(json_is_string(mxs_json_pointer(json, "links/self")));

    mxb_assert(mxs_json_pointer(json, "data") == json_object_get(json, "data"));
    mxb_assert(json_is_array(mxs_json_pointer(json, "data")));

    mxb_assert(json_is_object(mxs_json_pointer(json, "data/0")));
    mxb_assert(json_is_string(mxs_json_pointer(json, "data/0/id")));
    string s = json_string_value(mxs_json_pointer(json, "data/0/id"));
    mxb_assert(s == "server1");

    mxb_assert(json_is_object(mxs_json_pointer(json, "data/1")));
    mxb_assert(json_is_string(mxs_json_pointer(json, "data/1/id")));
    s = json_string_value(mxs_json_pointer(json, "data/1/id"));
    mxb_assert(s == "server2");

    mxb_assert(json_is_object(mxs_json_pointer(json, "data/0/attributes")));
    mxb_assert(json_is_object(mxs_json_pointer(json, "data/0/attributes/parameters")));
    mxb_assert(json_is_integer(mxs_json_pointer(json, "data/0/attributes/parameters/port")));
    int i = json_integer_value(mxs_json_pointer(json, "data/0/attributes/parameters/port"));
    mxb_assert(i == 3000);

    mxb_assert(json_is_array(mxs_json_pointer(json, "data/0/attributes/slaves")));
    mxb_assert(json_array_size(mxs_json_pointer(json, "data/0/attributes/slaves")) == 3);

    json_decref(json);

    return 0;
}

int test2()
{
    char *s;
    json_t* err;

    err = mxs_json_error("%s", "This is an error!");
    s = json_dumps(err, 0);
    printf("%s\n", s);
    mxb_assert(strcmp(s, "{\"errors\": [{\"detail\": \"This is an error!\"}]}") == 0);
    MXS_FREE(s);

    json_decref(err);

    err = mxs_json_error_append(NULL, "%s", "This is an error!");
    s = json_dumps(err, 0);
    printf("%s\n", s);
    mxb_assert(strcmp(s, "{\"errors\": [{\"detail\": \"This is an error!\"}]}") == 0);
    MXS_FREE(s);

    err = mxs_json_error_append(err, "%s", "This is another error!");
    s = json_dumps(err, 0);
    printf("%s\n", s);
    mxb_assert(strcmp(s,
                      "{\"errors\": [{\"detail\": \"This is an error!\"}, "
                      "{\"detail\": \"This is another error!\"}]}") == 0);
    MXS_FREE(s);

    json_decref(err);

    return 0;
}

int main(int argc, char** argv)
{
    int errors = 0;
    errors += test1();
    errors += test2();
    return 0;
}
