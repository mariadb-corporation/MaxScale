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
import Link from '@share/components/common/MxsSvgGraphs/Link'
import EntityLinkShape from '@share/components/common/MxsSvgGraphs/EntityLinkShape'
import EntityMarker from '@share/components/common/MxsSvgGraphs/EntityMarker'
import { TARGET_POS, EVENT_TYPES } from '@share/components/common/MxsSvgGraphs/config'
import { lodash } from '@share/utils/helpers'

export default class EntityLink extends Link {
    constructor({ linkConfig, shapeConfig }) {
        super(linkConfig)
        this.shapeConfig = shapeConfig
        this.shape = new EntityLinkShape(shapeConfig)
        this.data = []
        /**
         * TODO: pass only the config of the link and move reusable methods in Link
         * class to another class
         */
        this.marker = new EntityMarker({ linkInstance: this })
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
        else value = TARGET_POS.INTERSECT
        this.mutateLinkData({ link, key: 'targetPos', value })
    }

    // flat links into points and caching its link data and positions of the relational column
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

    pathGenerator(link) {
        const { points, yPosSrcTarget } = this.shape.genPathPoints(link)
        this.mutateLinkData({ link, key: 'pathPoints', value: points })
        this.mutateLinkData({ link, key: 'srcYPos', value: yPosSrcTarget.srcYPos })
        this.mutateLinkData({ link, key: 'targetYPos', value: yPosSrcTarget.targetYPos })
        return this.shape.createPath(points)
    }

    /**
     * Repositions overlapped points for each entity,
     * so that each point is visible and aligned in the row.
     */
    repositionOverlappedPoints() {
        const { rowHeight, rowOffset } = this.shapeConfig.entitySizeConfig
        Object.values(this.overlappedPoints()).forEach(points => {
            // divide the row into points.length equal parts
            const k = (rowHeight - rowOffset) / (points.length + 1)
            // reposition points
            points.forEach((point, i) => {
                const {
                    linkData: {
                        id,
                        pathPosData: { pathPoints, srcYPos, targetYPos },
                    },
                    isSrc,
                } = point
                const newY = (isSrc ? srcYPos.top : targetYPos.top) + (k * i + k)
                // update coord
                pathPoints[isSrc ? 'y0' : 'y1'] = newY
                //Redraw the link
                const containerEle = this.getLinkCtr(id)
                this.drawPath({
                    containerEle,
                    type: 'update',
                    pathGenerator: this.shape.createPath(pathPoints),
                })
                this.drawMarkers({ containerEle, type: 'update' })
            })
        })
    }
    /**
     * Draw source && target markers
     * @param {Object} param.containerEle - Container element of the marker
     * @param {String} param.type - enter or update
     */
    drawMarkers({ containerEle, type }) {
        this.marker.draw({ containerEle, type, isSrc: true })
        this.marker.draw({ containerEle, type })
    }

    /**
     * @param {Object} param.containerEle - container element of links to be drawn
     * @param {String} param.data - Links data
     */
    draw({ containerEle, data }) {
        this.data = data
        this.drawLinks({
            containerEle,
            data,
            pathGenerator: this.pathGenerator.bind(this),
            onEnter: linkCtr => linkCtr.each(this.setTargetXPos.bind(this)),
            onUpdate: linkCtr => linkCtr.each(this.setTargetXPos.bind(this)),
            afterEnter: linkCtr =>
                linkCtr.each(link =>
                    this.drawMarkers({ containerEle: this.getLinkCtr(link.id), type: 'enter' })
                ),
            afterUpdate: linkCtr =>
                linkCtr.each(link =>
                    this.drawMarkers({ containerEle: this.getLinkCtr(link.id), type: 'update' })
                ),
            mouseOver: linkCtr =>
                this.marker.changeMarkersOfLinkStyle({
                    elements: linkCtr,
                    eventType: EVENT_TYPES.HOVER,
                }),
            mouseOut: linkCtr =>
                this.marker.changeMarkersOfLinkStyle({
                    elements: linkCtr,
                    eventType: EVENT_TYPES.NONE,
                }),
        })
        this.repositionOverlappedPoints()
    }

    changeEntityLinkStyle(params) {
        this.changeLinksStyle(params)
        this.marker.changeMarkersStyle(params)
    }
}
