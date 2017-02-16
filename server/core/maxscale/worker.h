#pragma once
/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2019-07-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/worker.h>

MXS_BEGIN_DECLS

/**
 * Initialize the worker mechanism.
 *
 * To be called once at process startup. This will cause as many workers
 * to be created as the number of threads defined.
 */
void mxs_worker_init();

MXS_END_DECLS
