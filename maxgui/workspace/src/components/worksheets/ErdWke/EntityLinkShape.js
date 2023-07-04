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
import { LINK_SHAPES, TARGET_POS } from '@wsSrc/components/worksheets/ErdWke/config'
import { COL_ATTR_IDX_MAP, COL_ATTRS } from '@wsSrc/store/config'

export default class EntityLinkShape {
    constructor(graphConfig) {
        this.config = graphConfig.linkShape
        this.markerConfig = graphConfig.marker
        this.linkConfig = graphConfig.link
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

    /**
     * Return the y position of a node based on its dynamic height and
     * the provided attribute name
     * @param {Object} param.node - The node to reposition.
     * @param {String} param.attrId - The id of the relational column attribute
     * @param {Number} param.nodeHeight - Height of the node
     * @returns {Object} An object containing the y positions (top, center, bottom) of the node at
     * the provided relational attribute
     */
    getColYPos({ node, attrId, nodeHeight }) {
        const {
            entitySizeConfig: { rowHeight, rowOffset, headerHeight },
        } = this.config
        const { isAttrToAttr } = this.linkConfig

        const colIdx = node.data.definitions.cols.findIndex(
            c => c[COL_ATTR_IDX_MAP[COL_ATTRS.ID]] === attrId
        )
        const center = isAttrToAttr
            ? node.y +
              nodeHeight / 2 -
              (nodeHeight - headerHeight) +
              colIdx * rowHeight +
              rowHeight / 2
            : node.y

        return {
            top: isAttrToAttr
                ? center - (rowHeight - rowOffset) / 2
                : center - nodeHeight / 2 + rowOffset,
            center,
            bottom: isAttrToAttr
                ? center + (rowHeight - rowOffset) / 2
                : center + nodeHeight / 2 - rowOffset,
        }
    }

    /**
     * Get the y positions of source and target nodes
     * @returns {Object} An object containing the new y positions of the source and target nodes.
     */
    getYPositions() {
        const { src_attr_id, target_attr_id } = this.relationshipData
        const { srcHeight, targetHeight } = this
        return {
            srcYPos: this.getColYPos({
                node: this.source,
                attrId: src_attr_id,
                nodeHeight: srcHeight,
            }),
            targetYPos: this.getColYPos({
                node: this.target,
                attrId: target_attr_id,
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
            source: { x: srcX },
            target: { x: targetX },
            halfSrcWidth,
            halfTargetWidth,
        } = this
        const { RIGHT, LEFT, INTERSECT } = TARGET_POS
        const { type } = this.config

        let offset = 0
        /**
         * To enhance marker visibility in STRAIGHT shape, the link is drawn from the marker's
         * edge instead of the node's edge. The EntityMarker reverses the offset values of x0 and x1,
         * ensuring that the markers are drawn starting from the node's edge.
         */
        if (type === LINK_SHAPES.STRAIGHT) {
            const { width: markerWidth = 0 } = this.markerConfig
            offset = markerWidth
        }
        // D3 returns the mid point of the entities for source.x, target.x
        let x0 = srcX,
            x1 = targetX
        switch (this.targetPos) {
            case RIGHT: {
                x0 = srcX + halfSrcWidth + offset
                x1 = targetX - halfTargetWidth - offset
                break
            }
            case LEFT: {
                x0 = srcX - halfSrcWidth - offset
                x1 = targetX + halfTargetWidth + offset
                break
            }
            case INTERSECT: {
                // move x0 & x1 to the right edge of the nodes
                x0 = srcX + halfSrcWidth + offset
                x1 = targetX + halfTargetWidth + offset
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
        const { type } = this.config
        const { width: markerWidth = 0 } = this.markerConfig
        const offset = markerWidth * 1.5
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
