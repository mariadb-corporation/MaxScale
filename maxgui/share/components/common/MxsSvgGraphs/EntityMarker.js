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

import { TARGET_POS, CARDINALITY_SYMBOLS } from '@share/components/common/MxsSvgGraphs/config'

export default class EntityMarker {
    constructor() {
        this.linkInstance = {}
    }
    transform({ d, isSrc = false }) {
        const {
            pathPosData: { pathPoints, targetPos },
        } = d
        const { LEFT, INTERSECT } = TARGET_POS
        let x = pathPoints.x1,
            y = pathPoints.y1,
            k = 0
        if (targetPos === INTERSECT || targetPos === LEFT) k = 180
        if (isSrc) {
            x = pathPoints.x0
            y = pathPoints.y0
            k = 180
            if (targetPos === LEFT) k = 0
        }
        return `translate(${x}, ${y}) rotate(${k})`
    }
    /**
     * @param {Object} param.containerEle - Container element of the marker
     * @param {String} param.type - enter or update
     **/
    drawMarker({ containerEle, type, isSrc = false }) {
        const scope = this
        const { markerClass } = this.linkInstance.config
        const stroke = d => scope.linkInstance.getLinkStyles(d, 'color')
        const markerCtrClass = `entity-marker-${isSrc ? 'src' : 'target'}`
        const markerPathClass = `${markerClass} ${markerClass}-${isSrc ? 'src' : 'target'}`
        switch (type) {
            case 'enter':
                containerEle
                    .insert('g')
                    .attr('class', markerCtrClass)
                    .attr('transform', d => scope.transform({ d, isSrc }))
                    .append('path')
                    .attr('class', markerPathClass)
                    .attr('fill', 'none')
                    .attr('stroke', stroke)
                    .attr('d', d => scope.getMarker({ d, isSrc }))
                break
            case 'update':
                containerEle
                    .select(`g.${markerCtrClass}`)
                    .attr('transform', d => scope.transform({ d, isSrc }))
                    .select(`path.${markerPathClass.replace('.')}`)
                    .attr('stroke', stroke)
                    .attr('d', d => scope.getMarker({ d, isSrc }))
                break
        }
    }
    /**
     * @param {Object} param.containerEle - Container element of the marker
     * @param {String} param.type - enter or update
     **/
    draw(params) {
        this.linkInstance = params.linkInstance
        this.drawMarker({ ...params, isSrc: true })
        this.drawMarker(params)
    }

    getMarker({ d, isSrc }) {
        const {
            relationshipData: { type },
        } = d
        const [src, target] = type.split(':')
        return CARDINALITY_SYMBOLS[isSrc ? src : target]
    }
}
