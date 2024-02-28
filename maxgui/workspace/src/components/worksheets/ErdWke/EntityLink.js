/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-02-27
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import Link from '@share/components/common/MxsSvgGraphs/Link'
import { TARGET_POS } from '@share/components/common/MxsSvgGraphs/shapeConfig'
import { lodash } from '@share/utils/helpers'
import EntityLinkShape from '@wsSrc/components/worksheets/ErdWke/EntityLinkShape'
import EntityMarker from '@wsSrc/components/worksheets/ErdWke/EntityMarker'

export default class EntityLink extends Link {
    constructor(graphConfig) {
        super(graphConfig.link)
        this.graphConfig = graphConfig
        this.marker = new EntityMarker(graphConfig)
        this.shape = new EntityLinkShape(graphConfig)
        this.data = []
    }
    /**
     * @param {Object} params.link - The link object to mutate.
     * @param {String} params.key - The key to use for the updated property.
     * @param {*} params.value - The value to set for the updated property.
     */
    mutateLinkData({ link, key, value }) {
        if (!link.pathPosData) link.pathPosData = {}
        link.pathPosData[key] = value
    }

    /**
     * Checks the horizontal position of the target node and store the value
     * to targetPos property of the link data by mutating it.
     * @returns {String } Returns the relative position of the target node to the source node. TARGET_POS
     */
    setTargetXPos(link) {
        let value
        const { source, target } = link
        const { width: srcWidth } = source.size
        const { width: targetWidth } = target.size
        const halfSrcWidth = srcWidth / 2,
            halfTargetWidth = targetWidth / 2
        // use the smaller node for offset
        const offset = halfSrcWidth - halfTargetWidth ? halfTargetWidth : halfSrcWidth
        const srcZone = [source.x - halfSrcWidth + offset, source.x + halfSrcWidth - offset],
            targetZone = [target.x - halfTargetWidth, target.x + halfTargetWidth],
            isTargetRight = targetZone[0] - srcZone[1] >= 0,
            isTargetLeft = srcZone[0] - targetZone[1] >= 0

        if (isTargetRight) value = TARGET_POS.RIGHT
        else if (isTargetLeft) value = TARGET_POS.LEFT
        else value = TARGET_POS.INTERSECT_RIGHT
        this.mutateLinkData({ link, key: 'targetPos', value })
    }

    // flat links into points and caching its link data and positions of the relational attributes
    connPoints() {
        return Object.values(this.data).reduce((points, link) => {
            const {
                source,
                target,
                pathPosData: {
                    pathPoints: { x0, x1 },
                    srcYPos,
                    targetYPos,
                },
            } = link
            // range attribute helps to detect overlapped points
            points = [
                ...points,
                {
                    id: source.id,
                    range: `${x0},${srcYPos.center}`,
                    isSrc: true,
                    linkData: link,
                },
                {
                    id: target.id,
                    range: `${x1},${targetYPos.center}`,
                    isSrc: false,
                    linkData: link,
                },
            ]
            return points
        }, [])
    }
    /**
     * Generates a map of points that overlap in the `connPoints` array.
     * @returns {Object} - An object where the keys are link IDs and the values are arrays
     * of points that overlap. The array of points always has length >= 2
     */
    overlappedPoints() {
        // Group points have the same range
        let groupedPoints = lodash.groupBy(this.connPoints(), point => point.range)
        // get overlapped points and sort them
        return Object.keys(groupedPoints).reduce((acc, group) => {
            const points = groupedPoints[group]
            if (points.length > 1) {
                points.sort(
                    (a, b) =>
                        a.linkData.pathPosData[a.isSrc ? 'targetYPos' : 'srcYPos'].center -
                        b.linkData.pathPosData[b.isSrc ? 'targetYPos' : 'srcYPos'].center
                )
                acc[group] = points
            }
            return acc
        }, {})
    }
    /**
     * Repositions overlapped points for each entity,
     * so that each point is visible and aligned in the row.
     */
    repositionOverlappedPoints() {
        this.data.forEach(link => {
            this.setTargetXPos(link)
            const { points, yPosSrcTarget } = this.shape.getPoints(link)
            this.mutateLinkData({ link, key: 'pathPoints', value: points })
            this.mutateLinkData({ link, key: 'srcYPos', value: yPosSrcTarget.srcYPos })
            this.mutateLinkData({ link, key: 'targetYPos', value: yPosSrcTarget.targetYPos })
        })
        const {
            link: { isAttrToAttr },
            linkShape: {
                entitySizeConfig: { rowHeight, rowOffset },
            },
        } = this.graphConfig
        Object.values(this.overlappedPoints()).forEach(points => {
            const parts = points.length + 1
            // divide the row into points.length equal parts
            let k = (rowHeight - rowOffset) / parts

            // reposition points
            points.forEach((point, i) => {
                const {
                    linkData: {
                        pathPosData: { pathPoints, srcYPos, targetYPos },
                        source,
                        target,
                    },
                    isSrc,
                } = point
                if (!isAttrToAttr) k = (isSrc ? source.size.height : target.size.height) / parts
                const newY = (isSrc ? srcYPos.top : targetYPos.top) + (k * i + k)
                // update coord
                pathPoints[isSrc ? 'y0' : 'y1'] = newY
            })
        })
    }
    /**
     * Draw source && target markers
     * @param {Object} param.linkCtr - Link container element
     * @param {String} param.joinType - enter or update
     */
    drawMarkers({ linkCtr, joinType }) {
        this.marker.draw({ linkCtr, joinType, isSrc: true })
        this.marker.draw({ linkCtr, joinType })
    }
    /**
     * @param {object} param
     * @param {object} param.containerEle - container element of links to be drawn
     * @param {string} param.data - Links data
     * @param {object} param.events - events to be bound to link
     */
    draw({ containerEle, data, events }) {
        this.data = data
        this.repositionOverlappedPoints()
        this.drawLinks({
            containerEle,
            data,
            pathGenerator: link => this.shape.generate(link.pathPosData.pathPoints),
            afterEnter: this.drawMarkers.bind(this),
            afterUpdate: this.drawMarkers.bind(this),
            events,
        })
    }
}
