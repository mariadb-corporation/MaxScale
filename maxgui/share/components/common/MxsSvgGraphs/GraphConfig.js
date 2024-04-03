/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-04-03
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import { lodash } from '@share/utils/helpers'

export default class GraphConfig {
    constructor(config) {
        this.config = lodash.cloneDeep(config)
    }
    updateConfig({ path, value }) {
        lodash.set(this.config, path, value)
    }
}
