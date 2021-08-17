/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-08-17
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <fstream>
#include <map>
#include <maxbase/log.hh>
#include "../../../../../query_classifier/test/testreader.hh"
#include "../cache_storage_api.hh"
#include "../cache.hh"

using namespace std;

namespace
{

using StatementsByKeys = unordered_multimap<CacheKey, string>;

GWBUF* create_gwbuf(const string& s)
{
    size_t len = s.length();
    size_t payload_len = len + 1;
    size_t gwbuf_len = MYSQL_HEADER_LEN + payload_len;

    GWBUF* gwbuf = gwbuf_alloc(gwbuf_len);

    *((unsigned char*)((char*)GWBUF_DATA(gwbuf))) = payload_len;
    *((unsigned char*)((char*)GWBUF_DATA(gwbuf) + 1)) = (payload_len >> 8);
    *((unsigned char*)((char*)GWBUF_DATA(gwbuf) + 2)) = (payload_len >> 16);
    *((unsigned char*)((char*)GWBUF_DATA(gwbuf) + 3)) = 0x00;
    *((unsigned char*)((char*)GWBUF_DATA(gwbuf) + 4)) = 0x03;
    memcpy((char*)GWBUF_DATA(gwbuf) + 5, s.c_str(), len);

    return gwbuf;
}

void run(StatementsByKeys& stats, istream& in)
{
    mxs::TestReader reader(in);

    string stmt;
    while (reader.get_statement(stmt) == mxs::TestReader::RESULT_STMT)
    {
        GWBUF* pStmt = create_gwbuf(stmt);
        CacheKey key;

        Cache::get_default_key(nullptr, pStmt, &key);
        gwbuf_free(pStmt);

        auto range = stats.equal_range(key);

        if (range.first != stats.end())
        {
            bool header_printed = false;

            for (auto it = range.first; it != range.second; ++it)
            {
                if (stmt != it->second)
                {
                    if (!header_printed)
                    {
                        cout << "Statement: " << stmt << " clashes with:\n";
                        header_printed = true;
                    }
                    cout << "  " << it->second << endl;
                }
            }

            if (header_printed)
            {
                cout << endl;
            }
        }

        stats.emplace(key, stmt);
    }
}

void run(StatementsByKeys& stats, int argc, char** argv)
{
    for (int i = 0; i < argc; i++)
    {
        cout << argv[i] << endl;
        ifstream in(argv[i]);

        run(stats, in);
    }
}

}

int main(int argc, char** argv)
{
    mxb::Log log;
    StatementsByKeys stats;

    if (argc != 1)
    {
        run(stats, argc - 1, argv + 1);
    }
    else
    {
        run(stats, cin);
    }

    return 0;
}
