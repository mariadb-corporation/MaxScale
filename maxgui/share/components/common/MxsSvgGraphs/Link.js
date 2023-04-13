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
import { getLinkStyles, getRelatedLinks } from '@share/components/common/MxsSvgGraphs/utils'
import { select as d3Select } from 'd3-selection'
import { lodash } from '@share/utils/helpers'
import { t } from 'typy'
import defaultConfig, { EVENT_TYPES } from '@share/components/common/MxsSvgGraphs/config'

export default class Link {
    constructor(config) {
        this.config = lodash.merge(defaultConfig().link, t(config).safeObjectOrEmpty)
    }
    getStyle(link, styleNamePath) {
        return getLinkStyles({ link, styleNamePath, linkConfig: this.config })
    }
    /**
     * @param {Object} param.containerEle - Container element of the path
     * @param {String} param.type - enter or update
     * @param {Boolean} param.isInvisible - Draw an invisible path
     * @param {Function} param.pathGenerator - Path generator function
     **/
    drawPath({ containerEle, type, isInvisible, pathGenerator }) {
        const scope = this
        const { pathClass, invisiblePathClass } = this.config
        const className = isInvisible ? invisiblePathClass : pathClass

        const stroke = d => (isInvisible ? 'transparent' : scope.getStyle(d, 'color'))
        const strokeWidth = d =>
            isInvisible
                ? scope.getStyle(d, 'invisibleStrokeWidth')
                : scope.getStyle(d, 'strokeWidth')
        const strokeDashArray = d => (isInvisible ? 0 : scope.getStyle(d, 'dashArr'))

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
     * @param {Function} param.afterEnter - After links are entered into the DOM
     * @param {Function} param.afterUpdate - After links are updated in the DOM
     * @param {Function} param.mouseOver - mouseover link container element
     * @param {Function} param.mouseOut - mouseout link container element
     */
    drawLinks({
        containerEle,
        data,
        nodeIdPath = 'id',
        pathGenerator,
        onEnter,
        onUpdate,
        afterEnter,
        afterUpdate,
        mouseOver,
        mouseOut,
    }) {
        const scope = this
        const { containerClass } = this.config
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
                        .style('opacity', d => scope.getStyle(d, 'opacity'))
                        .on('mouseover', function() {
                            const linkCtr = d3Select(this)
                            scope.changeLinkStyle({
                                elements: linkCtr,
                                eventType: EVENT_TYPES.HOVER,
                            })
                            t(mouseOver).safeFunction(linkCtr)
                        })
                        .on('mouseout', function() {
                            const linkCtr = d3Select(this)
                            scope.changeLinkStyle({
                                elements: linkCtr,
                                eventType: EVENT_TYPES.NONE,
                            })
                            t(mouseOut).safeFunction(linkCtr)
                        })
                    t(onEnter).safeFunction(linkCtr)
                    this.drawPaths({ containerEle: linkCtr, type: 'enter', pathGenerator })
                    t(afterEnter).safeFunction(linkCtr)
                    return linkCtr
                },
                // update is called when node changes it size or its position
                update => {
                    const linkCtr = update
                    t(onUpdate).safeFunction(linkCtr)
                    this.drawPaths({ containerEle: linkCtr, type: 'update', pathGenerator })
                    t(afterUpdate).safeFunction(linkCtr)
                    return linkCtr
                },
                exit => exit.remove()
            )
    }
    /**
     * Change the style of a link or multiple links
     */
    changeLinkStyle({ elements, eventType }) {
        const scope = this
        const { pathClass } = this.config
        elements
            .style('opacity', d =>
                eventType ? scope.getStyle(d, `${eventType}.opacity`) : scope.getStyle(d, 'opacity')
            )
            .select(`path.${pathClass}`)
            .attr('stroke-width', d =>
                eventType
                    ? scope.getStyle(d, `${eventType}.strokeWidth`)
                    : scope.getStyle(d, 'strokeWidth')
            )
            .attr('stroke-dasharray', d =>
                eventType ? scope.getStyle(d, `${eventType}.dashArr`) : scope.getStyle(d, 'dashArr')
            )
    }
    /**
     * Change style of links having the same source and target
     */
    changeLinksStyle({ link, nodeIdPath, eventType }) {
        this.changeLinkStyle({
            elements: getRelatedLinks({
                link,
                linkCtrClass: this.config.containerClass,
                nodeIdPath,
            }),
            eventType,
        })
    }
}
