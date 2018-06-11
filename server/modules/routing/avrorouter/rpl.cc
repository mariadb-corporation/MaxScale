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

#include "rpl.hh"

#include <maxscale/debug.h>
#include <sstream>
#include <algorithm>

json_t* Column::to_json() const
{
    json_t* obj = json_object();
    json_object_set_new(obj, "name", json_string(name.c_str()));
    json_object_set_new(obj, "type", json_string(type.c_str()));
    json_object_set_new(obj, "length", json_integer(length));
    return obj;
}

Column Column::from_json(json_t* json)
{
    json_t* name = json_object_get(json, "name");
    json_t* type = json_object_get(json, "type");
    json_t* length = json_object_get(json, "length");

    if (name && json_is_string(name) &&
        type && json_is_string(type) &&
        length && json_is_integer(length))
    {
        return Column(json_string_value(name), json_string_value(type),
                      json_integer_value(length));
    }

    // Invalid JSON, return empty Column
    return Column("");
}

json_t* TableCreateEvent::to_json() const
{
    json_t* arr = json_array();

    for (auto it = columns.begin(); it != columns.end(); it++)
    {
        json_array_append_new(arr, it->to_json());
    }

    json_t* obj = json_object();
    json_object_set_new(obj, "table", json_string(table.c_str()));
    json_object_set_new(obj, "database", json_string(database.c_str()));
    json_object_set_new(obj, "version", json_integer(version));
    json_object_set_new(obj, "columns", arr);

    return obj;
}

STableCreateEvent TableCreateEvent::from_json(json_t* obj)
{
    STableCreateEvent rval;
    json_t* table = json_object_get(obj, "table");
    json_t* database = json_object_get(obj, "database");
    json_t* version = json_object_get(obj, "version");
    json_t* columns = json_object_get(obj, "columns");

    if (json_is_string(table) && json_is_string(database) &&
        json_is_integer(version) && json_is_array(columns))
    {
        std::string tbl = json_string_value(table);
        std::string db = json_string_value(database);
        int ver = json_integer_value(version);
        std::vector<Column> cols;
        size_t i = 0;
        json_t* val;

        json_array_foreach(columns, i, val)
        {
            cols.emplace_back(Column::from_json(val));
        }

        auto is_empty = [](const Column & col)
        {
            return col.name.empty();
        };

        if (std::none_of(cols.begin(), cols.end(), is_empty))
        {
            rval.reset(new TableCreateEvent(db, tbl, ver, std::move(cols)));
        }
    }

    return rval;
}

Rpl::Rpl(SERVICE* service, SRowEventHandler handler, gtid_pos_t gtid):
    m_handler(handler),
    m_service(service),
    m_binlog_checksum(0),
    m_event_types(0),
    m_gtid(gtid)
{
    /** For detection of CREATE/ALTER TABLE statements */
    static const char* create_table_regex = "(?i)create[a-z0-9[:space:]_]+table";
    static const char* alter_table_regex = "(?i)alter[[:space:]]+table";
    int pcreerr;
    size_t erroff;
    m_create_table_re = pcre2_compile((PCRE2_SPTR) create_table_regex, PCRE2_ZERO_TERMINATED,
                                      0, &pcreerr, &erroff, NULL);
    m_alter_table_re = pcre2_compile((PCRE2_SPTR) alter_table_regex, PCRE2_ZERO_TERMINATED,
                                     0, &pcreerr, &erroff, NULL);
    ss_info_dassert(m_create_table_re && m_alter_table_re,
                    "CREATE TABLE and ALTER TABLE regex compilation should not fail");

}

void gtid_pos_t::extract(const REP_HEADER& hdr, uint8_t* ptr)
{
    domain = extract_field(ptr + 8, 32);
    server_id = hdr.serverid;
    seq = extract_field(ptr, 64);
    event_num = 0;
    timestamp = hdr.timestamp;
}

bool gtid_pos_t::parse(const char* str)
{
    bool rval = false;
    char buf[strlen(str) + 1];
    strcpy(buf, str);
    char *saved, *dom = strtok_r(buf, ":-\n", &saved);
    char *serv_id = strtok_r(NULL, ":-\n", &saved);
    char *sequence = strtok_r(NULL, ":-\n", &saved);
    char *subseq = strtok_r(NULL, ":-\n", &saved);

    if (dom && serv_id && sequence)
    {
        domain = strtol(dom, NULL, 10);
        server_id = strtol(serv_id, NULL, 10);
        seq = strtol(sequence, NULL, 10);
        event_num = subseq ? strtol(subseq, NULL, 10) : 0;
        rval = true;
    }

    return rval;
}

gtid_pos_t gtid_pos_t::from_string(std::string str)
{
    gtid_pos_t gtid;
    gtid.parse(str.c_str());
    return gtid;
}

std::string gtid_pos_t::to_string() const
{
    std::stringstream ss;
    ss << domain << "-" << server_id << "-" << seq;
    return ss.str();
}

bool gtid_pos_t::empty() const
{
    return timestamp == 0 && domain == 0 && server_id == 0 && seq == 0 && event_num == 0;
}
