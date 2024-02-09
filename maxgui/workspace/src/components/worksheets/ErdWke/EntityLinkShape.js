/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-01-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { generateShape, getShapePoints } from '@share/components/common/MxsSvgGraphs/utils'
import { LINK_SHAPES, TARGET_POS } from '@share/components/common/MxsSvgGraphs/shapeConfig'

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

        const colIdx = Object.values(node.data.defs.col_map).findIndex(c => c.id === attrId)
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
        const { RIGHT, LEFT, INTERSECT_RIGHT } = TARGET_POS
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
            case INTERSECT_RIGHT: {
                // move x0 & x1 to the right edge of the nodes
                x0 = srcX + halfSrcWidth + offset
                x1 = targetX + halfTargetWidth + offset
                break
            }
        }
        return { x0, x1 }
    }

    /**
     * Generates points for creating <path/> values
     * @param {Object} linkData - The link object
     * @returns {Object} An object containing the path points and y positions of both src && target
     */
    getPoints(linkData) {
        this.setData(linkData)
        const { srcYPos, targetYPos } = this.getYPositions()
        const yValues = {
            y0: srcYPos.center,
            y1: targetYPos.center,
        }
        return {
            points: getShapePoints({
                data: {
                    ...this.getStartEndXValues(),
                    ...yValues,
                },
                type: this.config.type,
                offset: this.markerConfig.width,
                targetPos: this.targetPos,
            }),
            // position of y (top, center, bottom values)
            yPosSrcTarget: { srcYPos, targetYPos },
        }
    }

    // Generate a path from the given points
    generate(data) {
        return generateShape({ data, type: this.config.type })
    }
}
