/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-10-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
export default function vertCrossHair(chart) {
    if (chart.tooltip._active && chart.tooltip._active.length) {
        const scaleKeys = Object.keys(chart.scales)
        const idxOfYAxis = Object.keys(chart.scales).findIndex(scale => scale.includes('y-axis'))
        let activePoint = chart.tooltip._active[0],
            ctx = chart.ctx,
            y_axis = chart.scales[scaleKeys[idxOfYAxis]],
            x = activePoint.tooltipPosition().x,
            topY = y_axis.top,
            bottomY = y_axis.bottom
        ctx.save()
        ctx.beginPath()
        ctx.moveTo(x, topY)
        ctx.lineTo(x, bottomY)
        ctx.lineWidth = 2
        ctx.strokeStyle = '#e5e1e5'
        ctx.stroke()
        ctx.restore()
    }
}
