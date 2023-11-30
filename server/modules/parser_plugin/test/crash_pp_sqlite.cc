/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <stdio.h>
#include <maxbase/maxbase.hh>
#include <maxscale/built_in_modules.hh>
#include <maxscale/buffer.hh>
#include <maxscale/paths.hh>
#include <maxscale/testparser.hh>
#include "../../../core/internal/modules.hh"

#define MYSQL_HEADER_LEN 4

GWBUF* create_gwbuf(const char* s, size_t len)
{
    size_t payload_len = len + 1;
    size_t gwbuf_len = MYSQL_HEADER_LEN + payload_len;

    GWBUF* gwbuf = gwbuf_alloc(gwbuf_len);

    *((unsigned char*)((char*)GWBUF_DATA(gwbuf))) = payload_len;
    *((unsigned char*)((char*)GWBUF_DATA(gwbuf) + 1)) = (payload_len >> 8);
    *((unsigned char*)((char*)GWBUF_DATA(gwbuf) + 2)) = (payload_len >> 16);
    *((unsigned char*)((char*)GWBUF_DATA(gwbuf) + 3)) = 0x00;
    *((unsigned char*)((char*)GWBUF_DATA(gwbuf) + 4)) = 0x03;
    memcpy((char*)GWBUF_DATA(gwbuf) + 5, s, len);

    return gwbuf;
}

int main()
{
    maxbase::MaxBase init(MXB_LOG_TARGET_FS);

    mxs::set_libdir("../pp_sqlite");

    mxs::TestParser parser;

    const char s[] = "SELECT @@global.max_allowed_packet";

    GWBUF* pStmt = create_gwbuf(s, sizeof(s));   // Include superfluous NULL.

    // In 2.0.1 this crashed due to is_submitted_query() in pp_sqlite.c
    // being of the opinion that the statement was not the one to be
    // classified and hence an alien parse-tree being passed to sqlite3's
    // code generator.
    parser.parse(*pStmt, mxs::Parser::COLLECT_ALL);

    gwbuf_free(pStmt);

    return EXIT_SUCCESS;
}
