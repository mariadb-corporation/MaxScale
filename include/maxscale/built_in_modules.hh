/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-07-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

struct MXS_MODULE;

/**
 * Should only be used by the main executable to generate module info object.
 *
 * @return MariaDB-protocol module info
 */
MXS_MODULE* mariadbprotocol_info();

/**
 * Should only be used by the main executable to generate module info object.
 *
 * @return MariaDB-protocol module info
 */
MXS_MODULE* mariadbauthenticator_info();