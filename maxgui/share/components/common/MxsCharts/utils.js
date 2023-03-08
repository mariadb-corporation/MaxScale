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
import { select as d3Select } from 'd3-selection'

/**
 * @private
 * @param {Object} param.containerEle - Container element of the link
 * @param {String} param.type - enter or update
 * @param {Boolean} param.isInvisible - Draw an invisible link
 **/
function drawLink({
    containerEle,
    type,
    isInvisible,
    linkClassName,
    linkPathGenerator,
    linkStrokeGenerator,
}) {
    const className = isInvisible ? `${linkClassName}__invisible` : linkClassName
    const strokeWidth = isInvisible ? 12 : 2.5
    const strokeDasharray = isInvisible ? 0 : 5
    const strokeColor = isInvisible ? 'transparent' : linkStrokeGenerator
    switch (type) {
        case 'enter':
            containerEle
                .append('path')
                .attr('class', className)
                .attr('fill', 'none')
                .attr('stroke-width', strokeWidth)
                .attr('stroke-dasharray', strokeDasharray)
                .attr('stroke', strokeColor)
                .attr('d', linkPathGenerator)
            break
        case 'update':
            containerEle
                .select(`path.${className}`)
                .attr('stroke', strokeColor)
                .attr('d', linkPathGenerator)
            break
    }
}

/**
 * @public
 * The mouseover event on `.${linkCtrClassName}` element only works when the mouse is over the
 * "visiblePainted" path, not the space between dots. However, the thinness of the link makes
 * it difficult to trigger the event, so an additional invisible link with a large thickness
 * is drawn to help with this.
 * @param {Object} param.containerEle - container element of links to be drawn
 * @param {String} param.data - Links data
 * @param {Function} param.linkPathGenerator - Function to fill the value of the d attribute
 * @param {String|Function} param.linkStrokeGenerator - fill the value of the stroke attribute.
 * @param {String} param.linkCtrClassName - Class name of the container element of the link
 * @param {String} param.linkClassName - Class name of the link
 * @param {Function} param.onEnter - When links are entered into the DOM
 * @param {Function} param.onUpdate - When links are updated in the DOM
 */
export function drawLinks({
    containerEle,
    data,
    linkCtrClassName,
    linkClassName,
    linkPathGenerator,
    linkStrokeGenerator,
    onEnter,
    onUpdate,
}) {
    const drawLinkParams = { linkClassName, linkPathGenerator, linkStrokeGenerator }

    containerEle
        .selectAll(`.${linkCtrClassName}`)
        .data(data)
        .join(
            enter => {
                const linkCtr = enter
                    .insert('g')
                    .attr('class', `${linkCtrClassName} pointer`)
                    .style('opacity', 0.5)
                    .on('mouseover', function() {
                        d3Select(this)
                            .style('opacity', 1)
                            .style('z-index', 10)
                            .select(`path.${linkClassName}`)
                            .attr('stroke-dasharray', null)
                    })
                    .on('mouseout', function() {
                        d3Select(this)
                            .style('opacity', 0.5)
                            .style('z-index', 'unset')
                            .select(`path.${linkClassName}`)
                            .attr('stroke-dasharray', '5')
                    })
                drawLink({ containerEle: linkCtr, type: 'enter', ...drawLinkParams })
                drawLink({
                    containerEle: linkCtr,
                    type: 'enter',
                    isInvisible: true,
                    ...drawLinkParams,
                })
                if (onEnter) onEnter(linkCtr)
                return linkCtr
            },
            // update is called when node changes it size or its position
            update => {
                const linkCtr = update
                drawLink({ containerEle: linkCtr, type: 'update', ...drawLinkParams })
                drawLink({
                    containerEle: linkCtr,
                    type: 'update',
                    isInvisible: true,
                    ...drawLinkParams,
                })
                if (onUpdate) onUpdate(linkCtr)
                return linkCtr
            },
            exit => exit.remove()
        )
}
