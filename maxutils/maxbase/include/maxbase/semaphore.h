/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-02-27
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

/**
 * @file semaphore.h  Semaphores used by MaxScale.
 */

// As a minimal preparation for other environments than Linux, components
// include <maxbase/semaphore.h>,  instead of including <semaphore.h>
// directly.
#include <maxbase/cdefs.h>
#include <semaphore.h>
