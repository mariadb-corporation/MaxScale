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

#pragma once

#include <maxtest/ccdefs.hh>

namespace maxtest
{
int   create_tcp_socket();
char* get_ip(const char* host);
char* read_sc(int sock);
int   send_so(int sock, char* data);
char* cdc_auth_srt(char* user, char* password);
int   setnonblocking(int sock);
int   get_x_fl_from_json(const char* line, long long int* x1, long long int* fl);
}
