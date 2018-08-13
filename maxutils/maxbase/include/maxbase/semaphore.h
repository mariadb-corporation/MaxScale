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

/**
 * @file semaphore.h  Semaphores used by MaxScale.
 */

// As a minimal preparation for other environments than Linux, components
// include <maxbase/semaphore.h>,  instead of including <semaphore.h>
// directly.
#include <maxbase/cdefs.h>
#include <semaphore.h>
