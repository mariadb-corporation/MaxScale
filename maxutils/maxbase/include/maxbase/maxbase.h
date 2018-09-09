/*
 * Copyright (c) 2018 MariaDB Corporation Ab
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
#pragma once

#include <maxbase/cdefs.h>

/**
 * @brief Initializes the maxbase library
 *
 * Initializes the maxbase library, except for the log that must
 * be initialized separately, before or after this function is
 * called.
 *
 * @return True, if maxbase could be initialized, false otherwise.
 */
bool maxbase_init();

/**
 * @brief Finalizes the maxbase library
 *
 * This function should be called before program exit, if @c maxbase_init()
 * returned true.
 */
void maxbase_finish();
