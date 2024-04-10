/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-04-03
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
#include <maxscale/protocol/mariadb/mysql.hh>
#include "../../../core/internal/modules.hh"

#define MYSQL_HEADER_LEN 4

int main()
{
    maxbase::MaxBase init(MXB_LOG_TARGET_FS);

    mxs::set_libdir("../pp_sqlite");

    mxs::TestParser parser;

    const char s[] = "SELECT @@global.max_allowed_packet";

    GWBUF stmt = mariadb::create_query(std::string_view(s, sizeof(s)));   // Include superfluous NULL.

    // In 2.0.1 this crashed due to is_submitted_query() in pp_sqlite.c
    // being of the opinion that the statement was not the one to be
    // classified and hence an alien parse-tree being passed to sqlite3's
    // code generator.
    parser.parse(stmt, mxs::Parser::COLLECT_ALL);

    return EXIT_SUCCESS;
}
