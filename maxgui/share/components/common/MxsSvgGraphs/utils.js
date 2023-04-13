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
import { select as d3Select, selectAll as d3SelectAll } from 'd3-selection'
import { lodash } from '@share/utils/helpers'
import { t } from 'typy'

export function getLinkCtr(id) {
    return d3Select(`#${id}`)
}
/**
 * @param {Object} param.link - link object
 * @param {String} param.linkCtrClass - Link container class
 * @param {String|Array} [param.nodeIdPath='id'] - The path to the identifier field of a node
 */
export function getRelatedLinks({ link, linkCtrClass, nodeIdPath = 'id' }) {
    const srcId = lodash.objGet(link.source, nodeIdPath)
    const targetId = lodash.objGet(link.target, nodeIdPath)
    return d3SelectAll(`.${linkCtrClass}[src-id="${srcId}"][target-id="${targetId}"]`)
}
/**
 *
 * @param {Object} param.link - link data
 * @param {String} param.styleNamePath - style name path
 * @param {Object} param.linkConfig - global link config
 * @returns {String|Number} style value
 */
export function getLinkStyles({ link, styleNamePath, linkConfig }) {
    const linkStyle = lodash.cloneDeep(t(link, 'linkStyles').safeObjectOrEmpty)
    const globalValue = lodash.objGet(linkConfig, styleNamePath)
    // use global config style as a fallback value
    return lodash.objGet(
        linkStyle,
        styleNamePath,
        t(globalValue).isFunction ? globalValue(link) : globalValue
    )
}
