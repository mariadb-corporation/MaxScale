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
import { select as d3Select, selectAll as d3SelectAll } from 'd3-selection'
import { lodash } from '@share/utils/helpers'

const LINK_CTR_CLASS = 'link_container'
const LINK_LINE_CLASS = 'link_line'

export function getLinkCtr({ srcId, targetId }) {
    return d3Select(`.${LINK_CTR_CLASS}[src-id="${srcId}"][target-id="${targetId}"]`)
}

/**
 * @private
 * @param {Object} param.containerEle - Container element of the link
 * @param {String} param.type - enter or update
 * @param {Boolean} param.isInvisible - Draw an invisible link
 **/
export function drawLink({
    containerEle,
    type,
    isInvisible,
    linkPathGenerator,
    linkStrokeGenerator,
}) {
    const className = isInvisible ? `${LINK_LINE_CLASS}__invisible` : LINK_LINE_CLASS
    const strokeWidth = isInvisible ? 12 : 2.5

    const strokeColor = isInvisible ? 'transparent' : linkStrokeGenerator
    switch (type) {
        case 'enter':
            containerEle
                .append('path')
                .attr('class', className)
                .attr('fill', 'none')
                .attr('stroke-width', strokeWidth)
                .attr('stroke-dasharray', d => (isInvisible || d.isSolid ? 0 : 5))
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
 * The mouseover event on `.${LINK_CTR_CLASS}` element only works when the mouse is over the
 * "visiblePainted" path, not the space between dots. However, the thinness of the link makes
 * it difficult to trigger the event, so an additional invisible link with a large thickness
 * is drawn to help with this.
 * @param {Object} param.containerEle - container element of links to be drawn
 * @param {String} param.data - Links data
 * @param {String|Array} [param.idPath='id'] - The path to the identifier field of a node
 * @param {Function} param.linkPathGenerator - Function to fill the value of the d attribute
 * @param {String|Function} param.linkStrokeGenerator - fill the value of the stroke attribute.
 * @param {Function} param.onEnter - When links are entered into the DOM
 * @param {Function} param.onUpdate - When links are updated in the DOM
 */
export function drawLinks({
    containerEle,
    data,
    idPath = 'id',
    linkPathGenerator,
    linkStrokeGenerator,
    onEnter,
    onUpdate,
}) {
    const drawLinkParams = { linkPathGenerator, linkStrokeGenerator }

    containerEle
        .selectAll(`.${LINK_CTR_CLASS}`)
        .data(data)
        .join(
            enter => {
                const linkCtr = enter
                    .insert('g')
                    .attr('class', `${LINK_CTR_CLASS} pointer`)
                    .attr('src-id', d => lodash.objGet(d.source, idPath))
                    .attr('target-id', d => lodash.objGet(d.target, idPath))
                    .style('opacity', 0.5)
                    .on('mouseover', function() {
                        d3Select(this)
                            .style('opacity', 1)
                            .style('z-index', 10)
                            .select(`path.${LINK_LINE_CLASS}`)
                            .attr('stroke-dasharray', 0)
                    })
                    .on('mouseout', function() {
                        d3Select(this)
                            .style('opacity', 0.5)
                            .style('z-index', 'unset')
                            .select(`path.${LINK_LINE_CLASS}`)
                            .attr('stroke-dasharray', d => (d.isSolid ? 0 : 5))
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
/**
 * This helps to turn the dashed link to solid while dragging and vice versa.
 * @param {Object} param.link - link object
 * @param {String|Array} [param.idPath='id'] - The path to the identifier field of a node
 * @param {Boolean} param.isDragging - is dragging node
 */
export function changeLinkGroupStyle({ link, idPath = 'id', isDragging }) {
    const srcId = lodash.objGet(link.source, idPath)
    const targetId = lodash.objGet(link.target, idPath)
    d3SelectAll(`.${LINK_CTR_CLASS}[src-id="${srcId}"][target-id="${targetId}"]`)
        .style('opacity', isDragging ? 1 : 0.5)
        .style('z-index', isDragging ? 10 : 'unset')
        .select(`path.${LINK_LINE_CLASS}`)
        .attr('stroke-dasharray', d => (isDragging || d.isSolid ? 0 : 5))
}
