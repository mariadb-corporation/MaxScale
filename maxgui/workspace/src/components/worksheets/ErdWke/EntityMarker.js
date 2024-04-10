/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-04-03
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { getLinkStyles } from '@share/components/common/MxsSvgGraphs/utils'
import { CARDINALITY_SYMBOLS } from '@wsSrc/components/worksheets/ErdWke/config'
import { TARGET_POS, LINK_SHAPES } from '@share/components/common/MxsSvgGraphs/shapeConfig'

export default class EntityMarker {
    constructor(graphConfig) {
        this.config = graphConfig.marker
        this.linkConfig = graphConfig.link
        this.linkShape = graphConfig.linkShape
    }
    transform({ d, isSrc = false }) {
        const {
            pathPosData: {
                pathPoints: { x0, y0, x1, y1 },
                targetPos,
            },
        } = d
        const { type } = this.linkShape
        const { LEFT, RIGHT, INTERSECT_RIGHT } = TARGET_POS
        const { width } = this.config

        let x = isSrc ? x0 : x1,
            y = isSrc ? y0 : y1,
            offset = 0,
            z = 0
        // When shape is STRAIGHT, x0 and x1 values have deducted marker's width value.
        let offsetSrc = type === LINK_SHAPES.STRAIGHT ? -width : 0
        let offsetDest = type === LINK_SHAPES.STRAIGHT ? 0 : -width
        switch (targetPos) {
            case RIGHT:
                offset = isSrc ? offsetSrc : offsetDest
                z = isSrc ? 180 : 0
                break
            case LEFT:
                offset = isSrc ? offsetDest : offsetSrc
                z = isSrc ? 0 : 180
                break
            case INTERSECT_RIGHT:
                offset = offsetSrc
                z = 180
                break
        }
        return `translate(${x + offset}, ${y}) rotate(${z})`
    }
    /**
     * Marker reuses link's styles such as color, strokeWidth because
     * it isn't sensible to have its own styles, link and marker styles
     * should be consistent.
     */
    getStyle(link, styleNamePath) {
        return getLinkStyles({ link, styleNamePath, linkConfig: this.linkConfig })
    }
    /**
     * @param {Object} param.linkCtr - Link container element
     * @param {String} param.joinType - enter or update
     * @param {String} param.isSrc - whether the marker is placed on the source or target
     **/
    draw({ linkCtr, joinType, isSrc = false }) {
        const scope = this
        const { markerClass } = this.config
        const markerCtrClass = `entity-marker-${isSrc ? 'src' : 'target'}`
        const markerPathClass = `${markerClass}-${isSrc ? 'src' : 'target'}`

        let markers, paths
        switch (joinType) {
            case 'enter':
                markers = linkCtr.insert('g').attr('class', markerCtrClass)
                paths = markers.append('path').attr('class', markerPathClass)
                break
            case 'update':
                markers = linkCtr.select(`g.${markerCtrClass}`)
                paths = markers.select(`path.${markerPathClass}`)
                break
        }

        const stroke = d => scope.getStyle(d, 'color')
        const strokeWidth = d => scope.getStyle(d, 'strokeWidth')

        markers
            .attr('transform', d => scope.transform({ d, isSrc }))
            .attr('style', 'transform-box: fill-box;transform-origin:center')
        paths
            .attr('fill', 'white')
            .attr('stroke', stroke)
            .attr('stroke-width', strokeWidth)
            .attr('d', d => scope.getMarker({ d, isSrc }))
    }
    getMarker({ d, isSrc }) {
        const {
            relationshipData: { type },
        } = d
        const [src, target] = type.split(':')
        return CARDINALITY_SYMBOLS[isSrc ? src : target]
    }
}
