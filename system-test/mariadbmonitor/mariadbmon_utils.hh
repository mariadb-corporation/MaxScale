/*
 * Copyright (c) 2022 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-07-11
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#pragma once

#include <maxtest/testconnections.hh>

bool generate_traffic_and_check(TestConnections& test, mxt::MariaDB* conn, int insert_count);
bool generate_traffic_and_check_nosync(TestConnections& test, mxt::MariaDB* conn, int insert_count);

void prepare_log_bin_failover_test(TestConnections& test);
void cleanup_log_bin_failover_test(TestConnections& test);
