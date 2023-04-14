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
import { getLinkStyles } from '@share/components/common/MxsSvgGraphs/utils'
import { TARGET_POS, CARDINALITY_SYMBOLS } from '@share/components/common/MxsSvgGraphs/config'

export default class EntityMarker {
    constructor(graphConfig) {
        this.config = graphConfig.marker
        this.linkConfig = graphConfig.link
    }
    transform({ d, isSrc = false }) {
        const {
            pathPosData: { pathPoints, targetPos },
        } = d
        const { LEFT, RIGHT, INTERSECT } = TARGET_POS
        const { width } = this.config
        let offset = width
        switch (targetPos) {
            case RIGHT:
                offset = isSrc ? width : -width
                break
            case LEFT:
                offset = isSrc ? -width : width
                break
        }
        let x = pathPoints.x1 + offset,
            y = pathPoints.y1,
            z = 0
        if (targetPos === INTERSECT || targetPos === LEFT) z = 180
        if (isSrc) {
            x = pathPoints.x0 + offset
            y = pathPoints.y0
            z = 180
            if (targetPos === LEFT) z = 0
        }
        return `translate(${x}, ${y}) rotate(${z})`
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
     * @param {String} param.type - enter or update
     * @param {String} param.isSrc - whether the marker is placed on the source or target
     **/
    draw({ linkCtr, type, isSrc = false }) {
        const scope = this
        const { markerClass } = this.config
        const stroke = d => scope.getStyle(d, 'color')
        const strokeWidth = d => scope.getStyle(d, 'strokeWidth')
        const markerCtrClass = `entity-marker-${isSrc ? 'src' : 'target'}`
        const markerPathClass = `${markerClass}-${isSrc ? 'src' : 'target'}`
        switch (type) {
            case 'enter':
                linkCtr
                    .insert('g')
                    .attr('class', markerCtrClass)
                    .attr('transform', d => scope.transform({ d, isSrc }))
                    .append('path')
                    .attr('class', markerPathClass)
                    .attr('fill', 'white')
                    .attr('stroke', stroke)
                    .attr('stroke-width', strokeWidth)
                    .attr('d', d => scope.getMarker({ d, isSrc }))
                break
            case 'update':
                linkCtr
                    .select(`g.${markerCtrClass}`)
                    .attr('transform', d => scope.transform({ d, isSrc }))
                    .select(`path.${markerPathClass}`)
                    .attr('fill', 'white')
                    .attr('stroke', stroke)
                    .attr('stroke-width', strokeWidth)
                    .attr('d', d => scope.getMarker({ d, isSrc }))
                break
        }
    }
    getMarker({ d, isSrc }) {
        const {
            relationshipData: { type },
        } = d
        const [src, target] = type.split(':')
        return CARDINALITY_SYMBOLS[isSrc ? src : target]
    }
}
