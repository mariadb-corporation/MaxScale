/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-01-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#pragma once

/**
 * @brief get_my_ip Get IP address of machine where this code is executed as it is visible from remote machine
 * Connects to DNS port 53 of remote machine and gets own IP from socket info
 * @param remote_ip IP of remote machine
 * @param my_ip Pointer to result (own IP string)
 * @return 0 in case of success
 */
int get_my_ip(const char* remote_ip, char *my_ip );
