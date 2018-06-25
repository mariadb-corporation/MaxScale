#pragma once
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

#include <maxscale/worker.h>

MXS_BEGIN_DECLS

/**
 * Query whether worker should shutdown.
 *
 * @param worker  The worker in question.
 *
 * @return True, if the worker should shut down, false otherwise.
 */
bool mxs_worker_should_shutdown(MXS_WORKER* worker);

MXS_END_DECLS
