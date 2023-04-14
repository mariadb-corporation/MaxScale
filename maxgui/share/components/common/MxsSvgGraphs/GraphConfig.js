/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-03-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import defaultConfig from '@share/components/common/MxsSvgGraphs/config'
import { lodash } from '@share/utils/helpers'
import { t } from 'typy'

export default class GraphConfig {
    constructor(config) {
        let allConfig = lodash.merge(defaultConfig(), t(config).safeObjectOrEmpty)
        this.link = allConfig.link
        this.marker = allConfig.marker
        this.linkShape = allConfig.linkShape
    }

    updateConfig({ key, patch }) {
        this[key] = lodash.merge(this[key], patch)
    }
}
