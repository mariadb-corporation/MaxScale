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
import { lodash } from '@share/utils/helpers'
import { t } from 'typy'

/**
 *
 * @param {Object} param.link - link data
 * @param {String} param.styleNamePath - style name path
 * @param {Object} param.linkConfig - global link config
 * @returns {String|Number} style value
 */
export function getLinkStyles({ link, styleNamePath, linkConfig }) {
    const evtLinkStyles = lodash.cloneDeep(t(link, 'evtLinkStyles').safeObjectOrEmpty)
    const linkStyle = lodash.merge(
        lodash.cloneDeep(t(link, 'linkStyles').safeObjectOrEmpty),
        evtLinkStyles // event styles override link specific styles
    )
    const globalValue = lodash.objGet(linkConfig, styleNamePath)
    // use global config style as a fallback value
    return lodash.objGet(
        linkStyle,
        styleNamePath,
        t(globalValue).isFunction ? globalValue(link) : globalValue
    )
}
