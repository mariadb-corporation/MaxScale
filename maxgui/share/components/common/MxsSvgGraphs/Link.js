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
import { select as d3Select, selectAll as d3SelectAll } from 'd3-selection'
import { lodash } from '@share/utils/helpers'
import { t } from 'typy'
import defaultConfig from '@share/components/common/MxsSvgGraphs/config'

export default class Link {
    constructor(config) {
        this.config = lodash.merge(defaultConfig().link, t(config).safeObjectOrEmpty)
    }
    /**
     *
     * @param {Object} d - link data
     * @param {String} styleNamePath - style name path
     * @returns {String|Number} style value
     */
    getLinkStyles(d, styleNamePath) {
        const linkStyle = lodash.cloneDeep(t(d, 'linkStyles').safeObjectOrEmpty)
        const globalValue = lodash.objGet(this.config, styleNamePath)
        // use global config style as a fallback value
        return lodash.objGet(
            linkStyle,
            styleNamePath,
            t(globalValue).isFunction ? globalValue(d) : globalValue
        )
    }
    /**
     * @param {Object} param.containerEle - Container element of the path
     * @param {String} param.type - enter or update
     * @param {Boolean} param.isInvisible - Draw an invisible path
     **/
    drawPath({ containerEle, type, isInvisible, pathGenerator }) {
        const scope = this
        const { pathClass, invisiblePathClass } = this.config
        const className = isInvisible ? invisiblePathClass : pathClass

        const stroke = d => (isInvisible ? 'transparent' : scope.getLinkStyles(d, 'color'))
        const strokeWidth = d =>
            isInvisible
                ? scope.getLinkStyles(d, 'invisibleStrokeWidth')
                : scope.getLinkStyles(d, 'strokeWidth')
        const strokeDashArray = d => (isInvisible ? 0 : scope.getLinkStyles(d, 'dashArr'))

        switch (type) {
            case 'enter':
                containerEle
                    .append('path')
                    .attr('class', className)
                    .attr('fill', 'none')
                    .attr('stroke-width', strokeWidth)
                    .attr('stroke-dasharray', strokeDashArray)
                    .attr('stroke', stroke)
                    .attr('d', pathGenerator)
                break
            case 'update':
                containerEle
                    .select(`path.${className}`)
                    .attr('stroke', stroke)
                    .attr('d', pathGenerator)
                break
        }
    }
    drawPaths(params) {
        this.drawPath(params)
        this.drawPath({ ...params, isInvisible: true })
    }
    /**
     * The mouseover event on link <g/> element only works when the mouse is over the
     * "visiblePainted" of the <path/>, not the space between dots. However, the thinness
     * of the link makes it difficult to trigger the event, so an additional invisible
     * link with a large thickness is drawn to help with this.
     * @param {Object} param.containerEle - container element of links to be drawn
     * @param {String} param.data - Links data
     * @param {String|Array} [param.nodeIdPath='id'] - The path to the identifier field of a node
     * @param {Function} param.pathGenerator - Function to fill the value of the d attribute
     * @param {Function} param.onEnter - When links data are being prepared for entering into the DOM
     * @param {Function} param.onUpdate - When links are being updated in the DOM
     */
    drawLinks({ containerEle, data, nodeIdPath = 'id', pathGenerator, onEnter, onUpdate }) {
        const scope = this
        const { containerClass, pathClass } = this.config
        containerEle
            .selectAll(`.${containerClass}`)
            .data(data)
            .join(
                enter => {
                    const linkCtr = enter
                        .insert('g')
                        .attr('id', d => d.id)
                        .attr('class', `${containerClass} pointer`)
                        .attr('src-id', d => lodash.objGet(d.source, nodeIdPath))
                        .attr('target-id', d => lodash.objGet(d.target, nodeIdPath))
                        .style('opacity', d => scope.getLinkStyles(d, 'opacity'))
                        .on('mouseover', function() {
                            d3Select(this)
                                .style('opacity', d => scope.getLinkStyles(d, 'hover.opacity'))
                                .select(`path.${pathClass}`)
                                .attr('stroke-width', d =>
                                    scope.getLinkStyles(d, 'hover.strokeWidth')
                                )
                                .attr('stroke-dasharray', d =>
                                    scope.getLinkStyles(d, 'hover.dashArr')
                                )
                        })
                        .on('mouseout', function() {
                            d3Select(this)
                                .style('opacity', d => scope.getLinkStyles(d, 'opacity'))
                                .select(`path.${pathClass}`)
                                .attr('stroke-width', d => scope.getLinkStyles(d, 'strokeWidth'))
                                .attr('stroke-dasharray', d => scope.getLinkStyles(d, 'dashArr'))
                        })
                    t(onEnter).safeFunction(linkCtr)
                    this.drawPaths({ containerEle: linkCtr, type: 'enter', pathGenerator })
                    return linkCtr
                },
                // update is called when node changes it size or its position
                update => {
                    const linkCtr = update
                    t(onUpdate).safeFunction(linkCtr)
                    this.drawPaths({ containerEle: linkCtr, type: 'update', pathGenerator })
                    return linkCtr
                },
                exit => exit.remove()
            )
    }
    /**
     * @param {Object} param.link - link object
     * @param {String|Array} [param.nodeIdPath='id'] - The path to the identifier field of a node
     * @param {Boolean} param.isDragging - is dragging node
     */
    changeLinkStyle({ link, nodeIdPath = 'id', isDragging }) {
        const scope = this
        const { containerClass, pathClass } = this.config
        const srcId = lodash.objGet(link.source, nodeIdPath)
        const targetId = lodash.objGet(link.target, nodeIdPath)
        d3SelectAll(`.${containerClass}[src-id="${srcId}"][target-id="${targetId}"]`)
            .style('opacity', isDragging ? 1 : 0.5)
            .select(`path.${pathClass}`)
            .attr('stroke-width', d =>
                isDragging
                    ? scope.getLinkStyles(d, 'dragging.strokeWidth')
                    : scope.getLinkStyles(d, 'strokeWidth')
            )
            .attr('stroke-dasharray', d =>
                isDragging
                    ? scope.getLinkStyles(d, 'dragging.dashArr')
                    : scope.getLinkStyles(d, 'dashArr')
            )
    }
    getLinkCtr(id) {
        return d3Select(`#${id}`)
    }
}
