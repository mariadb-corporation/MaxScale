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

#include <maxscale/cppdefs.hh>

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

    ss_dassert(json);

    ss_dassert(mxs_json_pointer(json, "") == json);
    ss_dassert(mxs_json_pointer(json, "links") == json_object_get(json, "links"));
    ss_dassert(json_is_string(mxs_json_pointer(json, "links/self")));

    ss_dassert(mxs_json_pointer(json, "data") == json_object_get(json, "data"));
    ss_dassert(json_is_array(mxs_json_pointer(json, "data")));

    ss_dassert(json_is_object(mxs_json_pointer(json, "data/0")));
    ss_dassert(json_is_string(mxs_json_pointer(json, "data/0/id")));
    string s = json_string_value(mxs_json_pointer(json, "data/0/id"));
    ss_dassert(s == "server1");

    ss_dassert(json_is_object(mxs_json_pointer(json, "data/1")));
    ss_dassert(json_is_string(mxs_json_pointer(json, "data/1/id")));
    s = json_string_value(mxs_json_pointer(json, "data/1/id"));
    ss_dassert(s == "server2");

    ss_dassert(json_is_object(mxs_json_pointer(json, "data/0/attributes")));
    ss_dassert(json_is_object(mxs_json_pointer(json, "data/0/attributes/parameters")));
    ss_dassert(json_is_integer(mxs_json_pointer(json, "data/0/attributes/parameters/port")));
    int i = json_integer_value(mxs_json_pointer(json, "data/0/attributes/parameters/port"));
    ss_dassert(i == 3000);

    ss_dassert(json_is_array(mxs_json_pointer(json, "data/0/attributes/slaves")));
    ss_dassert(json_array_size(mxs_json_pointer(json, "data/0/attributes/slaves")) == 3);

    return 0;
}

int main(int argc, char** argv)
{
    test1();
    return 0;
}
