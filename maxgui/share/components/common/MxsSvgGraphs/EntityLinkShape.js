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
import defaultConfig, { LINK_SHAPES } from '@share/components/common/MxsSvgGraphs/config'

export default class EntityLinkShape {
    static LINK_SHAPES = LINK_SHAPES

    static TARGET_POS = {
        RIGHT: 'right',
        LEFT: 'left',
        INTERSECT: 'intersect',
    }

    constructor(config) {
        this.config = lodash.merge(defaultConfig().linkShape, t(config).safeObjectOrEmpty)
    }

    updateConfig(newConfig) {
        this.config = lodash.merge(this.config, newConfig)
    }

    getNodeSize(node) {
        return node.size
    }
    /**
     * Return the y position of a node based on its dynamic height and
     * the provided column name
     * @param {Object} param.node - The node to reposition.
     * @param {String} param.col - The name of the relational column
     * @param {Number} param.nodeHeight - Height of the node
     * @returns {Object} An object containing the y positions (top, center, bottom) of the node at
     * the provided relational column
     */
    getColYPos({ node, col, nodeHeight }) {
        const {
            entitySizeConfig: { rowHeight, rowOffset },
        } = this.config

        const colIdx = node.data.cols.findIndex(c => c.name === col)
        const center = node.y + nodeHeight / 2 - nodeHeight + colIdx * rowHeight + rowHeight / 2
        return {
            top: center - (rowHeight - rowOffset) / 2,
            center,
            bottom: center + (rowHeight - rowOffset) / 2,
        }
    }

    /**
     * Get the y positions of source and target nodes
     * @param {Object} linkData - Link data
     * @returns {Object} An object containing the new y positions of the source and target nodes.
     */
    getYPositions(linkData) {
        const {
            source,
            target,
            relationshipData: { source_col, target_col },
        } = linkData
        const { height: srcHeight } = this.getNodeSize(source)
        const { height: targetHeight } = this.getNodeSize(target)
        return {
            srcYPos: this.getColYPos({
                node: source,
                col: source_col,
                nodeHeight: srcHeight,
            }),
            targetYPos: this.getColYPos({
                node: target,
                col: target_col,
                nodeHeight: targetHeight,
            }),
        }
    }

    /**
     * Checks the horizontal position of the target node.
     * @param {Number} params.srcX - The horizontal position of the source node relative to the center.
     * @param {Number} params.targetX - The horizontal position of the target node relative to the center.
     * @param {Number} params.halfSrcWidth - Half the width of the source node.
     * @param {Number} params.halfTargetWidth - Half the width of the target node.
     * @returns {String } Returns the relative position of the target node to the source node. TARGET_POS
     */
    checkTargetPosX({ srcX, targetX, halfSrcWidth, halfTargetWidth }) {
        // use the smaller node for offset
        const offset = halfSrcWidth - halfTargetWidth ? halfTargetWidth : halfSrcWidth
        const srcZone = [srcX - halfSrcWidth + offset, srcX + halfSrcWidth - offset],
            targetZone = [targetX - halfTargetWidth, targetX + halfTargetWidth],
            isTargetRight = targetZone[0] - srcZone[1] >= 0,
            isTargetLeft = srcZone[0] - targetZone[1] >= 0

        if (isTargetRight) return EntityLinkShape.TARGET_POS.RIGHT
        else if (isTargetLeft) return EntityLinkShape.TARGET_POS.LEFT
        return EntityLinkShape.TARGET_POS.INTERSECT
    }

    /**
     * Get x values for the source and target node to form a straight line
     */
    getStartEndXValues({ srcX, targetX, halfSrcWidth, halfTargetWidth, targetPosType }) {
        const {
            entitySizeConfig: { markerWidth },
        } = this.config
        const { RIGHT, LEFT, INTERSECT } = EntityLinkShape.TARGET_POS
        let x0 = srcX,
            x1 = targetX
        switch (targetPosType) {
            case RIGHT: {
                x0 = srcX + halfSrcWidth + markerWidth
                x1 = targetX - halfTargetWidth - markerWidth
                break
            }
            case LEFT: {
                x0 = srcX - halfSrcWidth - markerWidth
                x1 = targetX + halfTargetWidth + markerWidth
                break
            }
            case INTERSECT: {
                // move x0 & x1 to the right edge of the nodes
                x0 = srcX + halfSrcWidth + markerWidth
                x1 = targetX + halfTargetWidth + markerWidth
                break
            }
        }
        return { x0, x1 }
    }

    /**
     * Get x values of source and target nodes based on shapeType
     * @param {Object} linkData - Link data
     * @returns {Object} An object containing the x values of the source and target
     * nodes, as well as the midpoint values of the link.
     */
    getValuesX(linkData) {
        const { source, target } = linkData
        const { width: srcWidth } = this.getNodeSize(source)
        const { width: targetWidth } = this.getNodeSize(target)
        const { type } = this.config
        const halfSrcWidth = srcWidth / 2,
            halfTargetWidth = targetWidth / 2
        // D3 returns the mid point of the entities for source.x, target.x
        const srcX = source.x,
            targetX = target.x
        const targetPosType = this.checkTargetPosX({ srcX, targetX, halfSrcWidth, halfTargetWidth })
        let values = this.getStartEndXValues({
            srcX,
            targetX,
            halfSrcWidth,
            halfTargetWidth,
            targetPosType,
        })
        const { ORTHO, ENTITY_RELATION } = EntityLinkShape.LINK_SHAPES
        switch (type) {
            case ORTHO:
            case ENTITY_RELATION: {
                values = this.getOrthoValuesX({ ...values, targetPosType })
            }
        }
        return values
    }

    /**
     * Get x values to form an orthogonal link or
     * app.diagrams.net entity relation link shape
     * @param {String} param.x0 - x value of source node.
     * @param {String} param.x1 - x value of target node.
     * @param {String} param.targetPosType - TARGET_POS value
     */
    getOrthoValuesX({ x0, x1, targetPosType }) {
        let midPointX, dx1, dx4, dx2, dx3
        const {
            type,
            entitySizeConfig: { markerWidth },
        } = this.config
        const offset = markerWidth / 2
        const isEntityRelationShape = type === EntityLinkShape.LINK_SHAPES.ENTITY_RELATION
        const { RIGHT, LEFT, INTERSECT } = EntityLinkShape.TARGET_POS
        switch (targetPosType) {
            case RIGHT: {
                midPointX = (x1 - x0) / 2
                if (midPointX <= offset || isEntityRelationShape) midPointX = offset
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
                if (midPointX <= offset || isEntityRelationShape) midPointX = offset
                dx1 = x0 - midPointX
                dx4 = x1 + midPointX
                if (dx1 - dx4 <= 0) {
                    dx2 = dx1
                    dx3 = dx4
                }
                break
            }
            case INTERSECT: {
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
        }
        return { x0, x1, dx1, dx2, dx3, dx4 }
    }

    // Generate a path from the given points
    createPath({ x0, x1, dx1, dx2, dx3, dx4, y0, y1 }) {
        const { type } = this.config
        const point0 = [x0, y0]
        const point5 = [x1, y1]
        const { ORTHO, ENTITY_RELATION } = EntityLinkShape.LINK_SHAPES
        switch (type) {
            case ORTHO:
            case ENTITY_RELATION: {
                const point1 = [dx1, y0]
                const point4 = [dx4, y1]
                if (dx2 && dx3) {
                    const point2 = [dx2, (y0 + y1) / 2],
                        point3 = [dx3, (y0 + y1) / 2]
                    return `M${point0} L${point1} L${point2} L${point3} L${point4} L${point5}`
                }
                return `M${point0} L${point1} L${point4} L${point5}`
            }
            // straight line
            default:
                return `M${point0} L${point5}`
        }
    }

    /**
     * Generates value for <path/> based on the given shape type, link object, source and
     * target size data, and marker width.
     * @param {Object} linkData - The link object
     * @returns {Object} An object containing the path and data objects.
     */
    genPath(linkData) {
        const { srcYPos, targetYPos } = this.getYPositions(linkData)
        const yValues = {
            y0: srcYPos.center,
            y1: targetYPos.center,
        }
        const xValues = this.getValuesX(linkData)

        const data = { ...xValues, ...yValues }
        // Store position of y (top, center, bottom values)
        linkData.pathData = { srcYPos, targetYPos }
        return {
            path: this.createPath({ ...data }),
            data,
        }
    }
}
