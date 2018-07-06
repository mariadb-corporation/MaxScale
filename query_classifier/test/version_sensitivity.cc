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

#include <iostream>
#include <maxscale/log_manager.h>
#include <maxscale/paths.h>
#include <maxscale/query_classifier.h>
#include <maxscale/server.h>
#include <maxscale/protocol/mysql.h>

using namespace std;

namespace
{

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

}

namespace
{

bool test(const string& s, uint32_t expected)
{
    GWBUF* pBuf = create_gwbuf(s);

    uint32_t type_mask = qc_get_type_mask(pBuf);

    gwbuf_free(pBuf);

    bool success = (type_mask == expected);

    if (!success)
    {
        cout << "error: " << s << " classified wrong." << endl;
    }

    return success;
}

int test()
{
    int rc = EXIT_SUCCESS;

    string valid_json("SELECT Json_Array(56, 3.1416, 'My name is \"Foo\"', NULL)");
    string invalid_json("SELECT Json_Foo(56, 3.1416, 'My name is \"Foo\"', NULL)");

    SERVER_VERSION sv;

    // pre-Json
    sv.major = 10;
    sv.minor = 0;
    sv.patch = 0;

    cout << "Testing pre-Json server." << endl;

    qc_set_server_version(server_encode_version(&sv));

    if (!test(valid_json, QUERY_TYPE_READ | QUERY_TYPE_WRITE))
    {
        rc = EXIT_FAILURE;
    }

    if (!test(invalid_json, QUERY_TYPE_READ | QUERY_TYPE_WRITE))
    {
        rc = EXIT_FAILURE;
    }

    cout << "Testing post-Json server." << endl;

    // post-Json
    sv.major = 10;
    sv.minor = 2;
    sv.patch = 3;

    qc_set_server_version(server_encode_version(&sv));

    if (!test(valid_json, QUERY_TYPE_READ))
    {
        rc = EXIT_FAILURE;
    }

    if (!test(invalid_json, QUERY_TYPE_READ | QUERY_TYPE_WRITE))
    {
        rc = EXIT_FAILURE;
    }

    return rc;
}

}

int main(int argc, char* argv[])
{
    int rc = EXIT_FAILURE;

    set_datadir(strdup("/tmp"));
    set_langdir(strdup("."));
    set_process_datadir(strdup("/tmp"));

    if (mxs_log_init(NULL, ".", MXS_LOG_TARGET_DEFAULT))
    {
        const char QC_LIB[] = "qc_sqlite";
        const char LIBDIR[] = "../qc_sqlite";

        set_libdir(strdup(LIBDIR));

        if (qc_setup(NULL, QC_SQL_MODE_DEFAULT, QC_LIB, NULL))
        {
            if (qc_process_init(QC_INIT_BOTH) && qc_thread_init(QC_INIT_BOTH))
            {
                rc = test();
            }
            else
            {
                cerr << "error: Could not perform process/thread initialization for " << QC_LIB << "." << endl;
            }
        }
        else
        {
            cerr << "error: Could not setup " << QC_LIB << "." << endl;
        }

        mxs_log_finish();
    }
    else
    {
        cerr << "error: Could not initialize log." << endl;
    }

    return rc;
}
