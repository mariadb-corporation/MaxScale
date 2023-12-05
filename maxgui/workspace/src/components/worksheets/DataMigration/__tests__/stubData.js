/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { ETL_STATUS } from '@wsSrc/store/config'

export const task = {
    id: 'c74d6e00-4263-11ee-a879-6f8dfc9ca55f',
    name: 'New migration',
    status: ETL_STATUS.INITIALIZING,
    active_stage_index: 0,
    is_prepare_etl: false,
    meta: { src_type: 'postgresql', dest_name: 'server_0' },
    res: {},
    logs: {
        '1': [],
        '2': [],
        '3': [],
    },
    created: 1692870680800,
    connections: [],
}
