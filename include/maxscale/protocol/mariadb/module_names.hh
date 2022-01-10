/*
 * Copyright (c) 2019 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-01-04
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

/**
 * Some module names are used in multiple files so the names are defined here.
 * As this file may be included before defining MXS_MODULE_NAME, ccdefs.hh should
 * not be included here.
 */
#define MXS_MARIADB_PROTOCOL_NAME          "MariaDBProtocol"
#define MXS_MARIADBAUTH_AUTHENTICATOR_NAME "MariaDBAuth"
