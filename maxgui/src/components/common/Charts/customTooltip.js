export default function customTooltip({ tooltipModel, tooltipId, scope }) {
    // Tooltip Element

    let tooltipEl = document.getElementById(tooltipId)

    // Create element on first render
    if (!tooltipEl) {
        tooltipEl = document.createElement('div')
        tooltipEl.id = tooltipId
        tooltipEl.className = ['chartjs-tooltip shadow-drop']
        tooltipEl.innerHTML = '<table></table>'
        document.body.appendChild(tooltipEl)
    }

    // Hide if no tooltip
    if (tooltipModel.opacity === 0) {
        tooltipEl.style.opacity = 0
        return
    }

    // Set caret Position
    tooltipEl.classList.remove('above', 'below', 'no-transform')
    if (tooltipModel.yAlign) {
        tooltipEl.classList.add(tooltipModel.yAlign)
    } else {
        tooltipEl.classList.add('no-transform')
    }

    // Set Text
    if (tooltipModel.body) {
        let titleLines = tooltipModel.title || []
        let bodyLines = tooltipModel.body.map(item => item.lines)

        let innerHtml = '<thead>'

        titleLines.forEach(title => {
            innerHtml += '<tr><th>' + title + '</th></tr>'
        })
        innerHtml += '</thead><tbody>'

        bodyLines.forEach((body, i) => {
            let colors = tooltipModel.labelColors[i]
            let style = 'background:' + colors.borderColor
            style += '; border-color:' + colors.borderColor
            style += '; border-width: 2px;margin-right:4px'
            let span = '<span class="chartjs-tooltip-key" style="' + style + '"></span>'
            innerHtml += '<tr><td>' + span + body + '</td></tr>'
        })
        innerHtml += '</tbody>'

        let tableRoot = tooltipEl.querySelector('table')
        tableRoot.innerHTML = innerHtml
    }
    const chart = scope._chart.canvas.getBoundingClientRect()
    // Display, position, and set styles for font
    tooltipEl.style.opacity = 1
    // this makes sure the tooltip wont go over client view width when realtime chart is used
    let left =
        chart.left + tooltipModel.caretX > chart.width + chart.left
            ? chart.width + chart.left
            : chart.left + tooltipModel.caretX

    tooltipEl.style.left = left + 'px'
    tooltipEl.style.top = chart.top + tooltipModel.caretY + 10 + 'px'
    tooltipEl.style.fontFamily = tooltipModel._bodyFontFamily
    tooltipEl.style.fontStyle = tooltipModel._bodyFontStyle
    tooltipEl.style.padding = tooltipModel.yPadding + 'px ' + tooltipModel.xPadding + 'px'
}
