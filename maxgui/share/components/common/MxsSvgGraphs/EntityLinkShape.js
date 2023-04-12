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
import defaultConfig, {
    LINK_SHAPES,
    TARGET_POS,
} from '@share/components/common/MxsSvgGraphs/config'

export default class EntityLinkShape {
    constructor(config) {
        this.config = lodash.merge(defaultConfig().linkShape, t(config).safeObjectOrEmpty)
    }

    setData(data) {
        const {
            source,
            target,
            pathPosData: { targetPos },
            relationshipData,
        } = data
        this.data = data
        this.source = source
        this.target = target

        const {
            source: {
                size: { width: srcWidth, height: srcHeight },
            },
            target: {
                size: { width: targetWidth, height: targetHeight },
            },
        } = data
        this.halfSrcWidth = srcWidth / 2
        this.halfTargetWidth = targetWidth / 2
        this.srcHeight = srcHeight
        this.targetHeight = targetHeight

        this.targetPos = targetPos
        this.relationshipData = relationshipData
    }

    updateConfig(newConfig) {
        this.config = lodash.merge(this.config, newConfig)
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
     * @returns {Object} An object containing the new y positions of the source and target nodes.
     */
    getYPositions() {
        const { source_col, target_col } = this.relationshipData
        const { srcHeight, targetHeight } = this
        return {
            srcYPos: this.getColYPos({
                node: this.source,
                col: source_col,
                nodeHeight: srcHeight,
            }),
            targetYPos: this.getColYPos({
                node: this.target,
                col: target_col,
                nodeHeight: targetHeight,
            }),
        }
    }

    /**
     * Get x values for the source and target node to form a straight line
     * @returns {Object} x values
     */
    getStartEndXValues() {
        const {
            entitySizeConfig: { markerWidth },
        } = this.config
        const {
            source: { x: srcX },
            target: { x: targetX },
            halfSrcWidth,
            halfTargetWidth,
        } = this
        const { RIGHT, LEFT, INTERSECT } = TARGET_POS

        // D3 returns the mid point of the entities for source.x, target.x
        let x0 = srcX,
            x1 = targetX
        switch (this.targetPos) {
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
     * @returns {Object} An object containing the x values of the source and target
     * nodes, as well as the midpoint values of the link.
     */
    getValuesX() {
        const { type } = this.config
        let values = this.getStartEndXValues()
        const { ORTHO, ENTITY_RELATION } = LINK_SHAPES
        switch (type) {
            case ORTHO:
            case ENTITY_RELATION: {
                values = this.getOrthoValuesX(values)
            }
        }
        return values
    }

    /**
     * Get x values to form an orthogonal link or
     * app.diagrams.net entity relation link shape
     * @param {String} param.x0 - x value of source node.
     * @param {String} param.x1 - x value of target node.
     * @returns {Object} x values
     */
    getOrthoValuesX({ x0, x1 }) {
        let midPointX, dx1, dx4, dx2, dx3
        const {
            type,
            entitySizeConfig: { markerWidth },
        } = this.config
        const offset = markerWidth / 2
        const isEntityRelationShape = type === LINK_SHAPES.ENTITY_RELATION
        const { RIGHT, LEFT, INTERSECT } = TARGET_POS
        switch (this.targetPos) {
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
        const { ORTHO, ENTITY_RELATION } = LINK_SHAPES
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
     * Generates points for creating <path/> values
     * @param {Object} linkData - The link object
     * @returns {Object} An object containing the path points and y positions of both src && target
     */
    genPathPoints(linkData) {
        this.setData(linkData)
        const { srcYPos, targetYPos } = this.getYPositions()
        const yValues = {
            y0: srcYPos.center,
            y1: targetYPos.center,
        }
        const xValues = this.getValuesX()
        return {
            points: { ...xValues, ...yValues },
            // position of y (top, center, bottom values)
            yPosSrcTarget: { srcYPos, targetYPos },
        }
    }
}
