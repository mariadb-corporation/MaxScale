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
import { lodash } from '@share/utils/helpers'
import { t } from 'typy'
import { LINK_SHAPES, TARGET_POS } from '@share/components/common/MxsSvgGraphs/shapeConfig'

/**
 *
 * @param {Object} param.link - link data
 * @param {String} param.styleNamePath - style name path
 * @param {Object} param.linkConfig - global link config
 * @returns {String|Number} style value
 */
export function getLinkStyles({ link, styleNamePath, linkConfig }) {
    const evtStyles = lodash.cloneDeep(t(link, 'evtStyles').safeObjectOrEmpty)
    const linkStyle = lodash.merge(
        lodash.cloneDeep(t(link, 'styles').safeObjectOrEmpty),
        evtStyles // event styles override link specific styles
    )
    const globalValue = lodash.objGet(linkConfig, styleNamePath)
    // use global config style as a fallback value
    return lodash.objGet(
        linkStyle,
        styleNamePath,
        t(globalValue).isFunction ? globalValue(link) : globalValue
    )
}

/**
 * @param {object} param
 * @param {number} param.data.x0 - start point x
 * @param {number} param.data.y0 - start point y
 * @param {number} param.data.x1 - end point y
 * @param {number} param.data.y1 - end point y
 * @param {LINK_SHAPES} param.type - LINK_SHAPES type
 * @param {TARGET_POS} param.targetPos - TARGET_POS value
 * @param {number} param.offset -  offset value
 * @returns {object}
 */
function getOrthoValues({ data: { x0, y0, x1, y1 }, type, targetPos, offset = 0 }) {
    let midPointX, dx1, dx4, dx2, dx3
    const isEntityRelationShape = type === LINK_SHAPES.ENTITY_RELATION
    const { RIGHT, LEFT, INTERSECT_RIGHT, INTERSECT_LEFT } = TARGET_POS
    switch (targetPos) {
        case RIGHT: {
            midPointX = (x1 - x0) / 2
            if (midPointX <= offset || isEntityRelationShape) {
                midPointX = offset
                if (isEntityRelationShape) midPointX *= 2
            }
            dx1 = x0 + midPointX
            dx4 = x1 - midPointX
            if (dx4 - dx1 <= 0) {
                dx2 = dx1
                dx3 = dx4
            }
            break
        }
        case LEFT: {
            midPointX = (x0 - x1) / 2
            if (midPointX <= offset || isEntityRelationShape) {
                midPointX = offset
                if (isEntityRelationShape) midPointX *= 2
            }
            dx1 = x0 - midPointX
            dx4 = x1 + midPointX
            if (dx1 - dx4 <= 0) {
                dx2 = dx1
                dx3 = dx4
            }
            break
        }
        case INTERSECT_RIGHT: {
            dx1 = x1 + offset
            dx4 = dx1
            if (dx1 - offset <= x0) {
                dx1 = x0 + offset
                if (dx4 - dx1 <= 0) {
                    dx2 = dx1
                    dx3 = dx1
                }
            }
            if (dx4 <= dx1) dx4 = dx1
            break
        }
        case INTERSECT_LEFT: {
            midPointX = (x0 - x1) / 2
            if (midPointX <= offset || isEntityRelationShape) {
                midPointX = offset
                if (isEntityRelationShape) midPointX *= 2
            }
            dx1 = x0 - midPointX
            dx2 = dx1
            dx3 = dx1
            dx4 = dx1
            break
        }
    }
    return { x0, y0, x1, y1, dx1, dx2, dx3, dx4 }
}

function getStartEndPoints({ x0, y0, x1, y1 }) {
    return { startPoint: [x0, y0], endPoint: [x1, y1] }
}

/**
 * @returns {object} point values
 */
export function getShapePoints({ data, type, offset, targetPos }) {
    const { ORTHO, ENTITY_RELATION } = LINK_SHAPES
    switch (type) {
        case ORTHO:
        case ENTITY_RELATION:
            return getOrthoValues({ type, data, targetPos, offset })
        default:
            return data
    }
}

/**
 * Create a shape based on start and end points
 * @returns {string} - path data value
 */
export function createShape({ type, offset, data, targetPos }) {
    const { ORTHO, ENTITY_RELATION } = LINK_SHAPES
    switch (type) {
        case ORTHO:
        case ENTITY_RELATION:
            return generateShape({ data: getOrthoValues({ type, data, targetPos, offset }), type })
        default:
            return generateShape({ data, type })
    }
}

/**
 * Generate shape from existing points
 * @param {object} param.data
 * @param {LINK_SHAPES} param.type - LINK_SHAPES type
 * @returns {string} - path data value
 */
export function generateShape({ data, type }) {
    const { ORTHO, ENTITY_RELATION } = LINK_SHAPES
    switch (type) {
        case ORTHO:
        case ENTITY_RELATION: {
            const { startPoint, endPoint } = getStartEndPoints(data)
            const { y0, y1, dx1, dx2, dx3, dx4 } = data
            const point1 = [dx1, y0]
            const point4 = [dx4, y1]
            if (dx2 && dx3) {
                const point2 = [dx2, (y0 + y1) / 2],
                    point3 = [dx3, (y0 + y1) / 2]
                return `M${startPoint} L${point1} L${point2} L${point3} L${point4} L${endPoint}`
            }
            return `M${startPoint} L${point1} L${point4} L${endPoint}`
        }
        // straight line
        default: {
            const { startPoint, endPoint } = getStartEndPoints(data)
            return `M${startPoint} L${endPoint}`
        }
    }
}
