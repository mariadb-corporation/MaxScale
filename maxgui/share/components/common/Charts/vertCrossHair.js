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
