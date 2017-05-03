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

#include <maxscale/json_api.h>

#include <string>

#include <maxscale/config.h>
#include <jansson.h>

using std::string;

static json_t* self_link(const char* host, const char* endpoint)
{
    json_t* self_link = json_object();
    string links = host;
    links += endpoint;
    json_object_set_new(self_link, CN_SELF, json_string(links.c_str()));

    return self_link;
}

json_t* mxs_json_resource(const char* host, const char* self, json_t* data)
{
    ss_dassert(data && (json_is_array(data) || json_is_object(data)));
    json_t* rval = json_object();
    json_object_set_new(rval, CN_LINKS, self_link(host, self));
    json_object_set_new(rval, CN_DATA, data);
    return rval;
}

json_t* mxs_json_relationship(const char* host, const char* endpoint)
{
    json_t* rel = json_object();

    /** Add the relation self link */
    json_object_set_new(rel, CN_LINKS, self_link(host, endpoint));

    /** Add empty array of relations */
    json_object_set_new(rel, CN_DATA, json_array());
    return rel;
}

void mxs_json_add_relation(json_t* rel, const char* id, const char* type)
{
    json_t* data = json_object_get(rel, CN_DATA);
    ss_dassert(data && json_is_array(data));

    json_t* obj = json_object();
    json_object_set_new(obj, CN_ID, json_string(id));
    json_object_set_new(obj, CN_TYPE, json_string(type));
    json_array_append(data, obj);
}
