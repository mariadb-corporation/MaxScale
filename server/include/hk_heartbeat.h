#ifndef _HK_HEARTBEAT_H
#define _HK_HEARTBEAT_H

/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl.
 *
 * Change Date: 2019-07-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * The global housekeeper heartbeat value. This value is incremented
 * every 100 milliseconds and may be used for crude timing etc.
 */

extern long hkheartbeat;

#endif
