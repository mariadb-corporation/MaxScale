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
