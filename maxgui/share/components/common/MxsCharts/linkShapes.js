/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-02-21
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

export const LINK_SHAPES = {
    ORTHO: 'Orthogonal',
    ENTITY_RELATION: 'Entity Relation',
    STRAIGHT: 'Straight',
}
/**
 * Return the y position of a node based on its dynamic height and
 * the provided column name
 * @param {Object} param.node - The node to reposition.
 * @param {String} param.col - The name of the relational column
 * @returns {Object} An object containing the y positions (top, center, bottom) of the node at
 * the provided relational column
 */
function getColYPos({
    node,
    col,
    nodeHeight,
    entitySizeData: { headerHeight, rowHeight, rowOffset },
}) {
    const colIdx = node.data.cols.findIndex(c => c.name === col)
    const center =
        node.y + nodeHeight / 2 - (nodeHeight - headerHeight) + colIdx * rowHeight + rowHeight / 2
    return {
        top: center - (rowHeight - rowOffset) / 2,
        center,
        bottom: center + (rowHeight - rowOffset) / 2,
    }
}
/**
 * TODO: Add getNodeYPos
 * getNodeYPos: for drawing a relationship link between two
 * entities.
 * getColYPos: for drawing FK links. This is the edit mode
 * where the user can add or delete FK.
 * Get the y positions of source and target nodes
 * @param {Object} param.linkData - link object
 * @returns {Object} An object containing the new y positions of the source and target nodes.
 */
function getYPositions({ linkData, srcHeight, targetHeight, entitySizeData }) {
    const {
        source,
        target,
        relationshipData: { source_col, target_col },
    } = linkData
    return {
        srcYPos: getColYPos({
            node: source,
            col: source_col,
            nodeHeight: srcHeight,
            entitySizeData,
        }),
        targetYPos: getColYPos({
            node: target,
            col: target_col,
            nodeHeight: targetHeight,
            entitySizeData,
        }),
    }
}

/**
 * Checks the horizontal position of the target node.
 * @param {Number} params.srcX - The horizontal position of the source node relative to the center.
 * @param {Number} params.targetX - The horizontal position of the target node relative to the center.
 * @param {Number} params.halfSrcWidth - Half the width of the source node.
 * @param {Number} params.halfTargetWidth - Half the width of the target node.
 * @returns {String} Returns the relative position of the target node to the source node.
 * "target-right" if the target node is to the right of the source node.
 * "target-left" if the target node is to the left of the source node.
 * "intersect" if the target node intersects with the source node
 */
function checkTargetPosX({ srcX, targetX, halfSrcWidth, halfTargetWidth }) {
    // use the smaller node for offset
    const offset = halfSrcWidth - halfTargetWidth ? halfTargetWidth : halfSrcWidth
    const srcZone = [srcX - halfSrcWidth + offset, srcX + halfSrcWidth - offset],
        targetZone = [targetX - halfTargetWidth, targetX + halfTargetWidth],
        isTargetRight = targetZone[0] - srcZone[1] >= 0,
        isTargetLeft = srcZone[0] - targetZone[1] >= 0

    if (isTargetRight) return 'target-right'
    else if (isTargetLeft) return 'target-left'
    return 'intersect'
}

/**
 * Get x values for the source and target node to form a straight line
 */
function getStartEndXValues({ srcX, targetX, halfSrcWidth, halfTargetWidth, targetPosType }) {
    let x0 = srcX,
        x1 = targetX
    switch (targetPosType) {
        case 'target-right': {
            x0 = srcX + halfSrcWidth
            x1 = targetX - halfTargetWidth
            break
        }
        case 'target-left': {
            x0 = srcX - halfSrcWidth
            x1 = targetX + halfTargetWidth
            break
        }
        case 'intersect': {
            // move x0 & x1 to the right edge of the nodes
            x0 = srcX + halfSrcWidth
            x1 = targetX + halfTargetWidth
            break
        }
    }
    return { x0, x1 }
}

/**
 * Get x values of source and target nodes based on shapeType
 * @param {Object} param.srcWidth - The width of the source node.
 * @param {Object} param.targetWidth - The width of the target node.
 * @returns {Object} An object containing the x values of the source and target
 * nodes, as well as the midpoint values of the link.
 */
function getValuesX({ shapeType, source, target, markerWidth, srcWidth, targetWidth }) {
    const halfSrcWidth = srcWidth / 2,
        halfTargetWidth = targetWidth / 2

    // D3 returns the mid point of the entities for source.x, target.x
    const srcX = source.x,
        targetX = target.x
    const targetPosType = checkTargetPosX({ srcX, targetX, halfSrcWidth, halfTargetWidth })
    let values = getStartEndXValues({ srcX, targetX, halfSrcWidth, halfTargetWidth, targetPosType })
    switch (shapeType) {
        case LINK_SHAPES.ORTHO:
        case LINK_SHAPES.ENTITY_RELATION: {
            values = getOrthoValuesX({ ...values, targetPosType, markerWidth, shapeType })
        }
    }
    return values
}

/**
 * Get x values to form an orthogonal link or
 * app.diagrams.net entity relation link shape
 * @param {String} param.x0 - x value of source node.
 * @param {String} param.x1 - x value of target node.
 */
function getOrthoValuesX({ x0, x1, targetPosType, markerWidth, shapeType }) {
    let midPointX, dx1, dx4, dx2, dx3
    const isEntityRelationShape = shapeType === LINK_SHAPES.ENTITY_RELATION
    switch (targetPosType) {
        case 'target-right': {
            midPointX = (x1 - x0) / 2
            if (midPointX <= markerWidth || isEntityRelationShape) midPointX = markerWidth
            dx1 = x0 + midPointX
            dx4 = x1 - midPointX
            if (dx4 - dx1 <= 0) {
                dx2 = dx1
                dx3 = dx4
            }
            break
        }
        case 'target-left': {
            midPointX = (x0 - x1) / 2
            if (midPointX <= markerWidth || isEntityRelationShape) midPointX = markerWidth
            dx1 = x0 - midPointX
            dx4 = x1 + midPointX
            if (dx1 - dx4 <= 0) {
                dx2 = dx1
                dx3 = dx4
            }
            break
        }
        case 'intersect': {
            dx1 = x1 + markerWidth
            dx4 = dx1
            if (dx1 - markerWidth <= x0) {
                dx1 = x0 + markerWidth
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
export function createPath({ shapeType, x0, x1, dx1, dx2, dx3, dx4, y0, y1 }) {
    const point0 = [x0, y0]
    const point5 = [x1, y1]
    switch (shapeType) {
        case LINK_SHAPES.ORTHO:
        case LINK_SHAPES.ENTITY_RELATION: {
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
 * @param {String} param.shapeType - The type of shape to generate.
 * @param {Object} param.linkData - The link object
 * @param {Object} param.nodeSizeMap - The size map (width, height)
 * @param {Number} param.entitySizeData.headerHeight - The header height of the entity
 * @param {Number} param.entitySizeData.rowHeight - The row height of the entity
 * @param {Number} param.entitySizeData.rowOffset - The offset value of the row height
 * @param {Number} param.entitySizeData.markerWidth - The width of the marker at the end of the path.
 * @returns {Object} An object containing the path and data objects.
 */
export function genPath({ shapeType, linkData, entitySizeData, nodeSizeMap }) {
    const { source, target } = linkData
    const { width: srcWidth, height: srcHeight } = nodeSizeMap[source.id]
    const { width: targetWidth, height: targetHeight } = nodeSizeMap[target.id]

    const { srcYPos, targetYPos } = getYPositions({
        linkData,
        srcHeight,
        targetHeight,
        entitySizeData,
    })

    const yValues = {
        y0: srcYPos.center,
        y1: targetYPos.center,
    }

    const xValues = getValuesX({
        shapeType,
        source,
        target,
        srcWidth,
        targetWidth,
        markerWidth: entitySizeData.markerWidth,
    })

    const data = { ...xValues, ...yValues }
    // Store position of y (top, center, bottom values)
    linkData.pathData = { srcYPos, targetYPos }
    return {
        path: createPath({ shapeType, ...data }),
        data,
    }
}
