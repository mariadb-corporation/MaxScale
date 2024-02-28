/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-02-27
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { select as d3Select } from 'd3-selection'
import { getLinkStyles } from '@share/components/common/MxsSvgGraphs/utils'
import { lodash } from '@share/utils/helpers'
import { t } from 'typy'

export default class Link {
    constructor(config) {
        this.config = config
    }
    getStyle(link, styleNamePath) {
        return getLinkStyles({ link, styleNamePath, linkConfig: this.config })
    }
    /**
     * @param {String} param.joinType - enter or update
     * @param {Object} param.linkCtr - Container element of the path
     * @param {Boolean} param.isInvisible - Draw an invisible path
     * @param {Function} param.pathGenerator - Path generator function
     **/
    drawPath({ joinType, linkCtr, isInvisible, pathGenerator }) {
        const scope = this
        const { pathClass, invisiblePathClass } = this.config
        const className = isInvisible ? invisiblePathClass : pathClass

        let paths
        switch (joinType) {
            case 'enter':
                paths = linkCtr.append('path').attr('class', className)
                break
            case 'update':
                paths = linkCtr.select(`path.${className}`)
                break
        }

        const stroke = d =>
            isInvisible ? scope.getStyle(d, 'invisibleHighlightColor') : scope.getStyle(d, 'color')
        const strokeWidth = d =>
            isInvisible
                ? scope.getStyle(d, 'invisibleStrokeWidth')
                : scope.getStyle(d, 'strokeWidth')
        const strokeDashArray = d => (isInvisible ? 0 : scope.getStyle(d, 'dashArr'))
        const opacity = d =>
            isInvisible ? scope.getStyle(d, 'invisibleOpacity') : scope.getStyle(d, 'opacity')

        paths
            .attr('fill', 'none')
            .attr('opacity', opacity)
            .attr('stroke-width', strokeWidth)
            .attr('stroke-dasharray', strokeDashArray)
            .attr('stroke', stroke)
            .attr('d', pathGenerator)
    }
    drawPaths(params) {
        this.drawPath({ ...params, isInvisible: true })
        this.drawPath(params)
    }

    afterJoinProcess({ joinType, linkCtr, pathGenerator, cb }) {
        this.drawPaths({ joinType, linkCtr, pathGenerator })
        t(cb).safeFunction({ linkCtr, joinType })
    }

    /**
     * The mouseover event on link <g/> element only works when the mouse is over the
     * "visiblePainted" of the <path/>, not the space between dots. However, the thinness
     * of the link makes it difficult to trigger the event, so an additional invisible
     * link with a large thickness is drawn to help with this.
     * @param {object} param.containerEle - container element of links to be drawn
     * @param {string} param.data - Links data
     * @param {string|array} [param.nodeIdPath='id'] - The path to the identifier field of a node
     * @param {function} param.pathGenerator - Function to fill the value of the d attribute
     * @param {function} param.afterEnter - After links are entered into the DOM
     * @param {function} param.afterUpdate - After links are updated in the DOM
     * @param {object} param.events - event handlers. e.g. { mouseover: handler() }
     */
    drawLinks({
        containerEle,
        data,
        nodeIdPath = 'id',
        pathGenerator,
        afterEnter,
        afterUpdate,
        events,
    }) {
        const { containerClass } = this.config
        containerEle
            .selectAll(`.${containerClass}`)
            .data(data)
            .join(
                enter => {
                    const linkCtr = enter
                        .insert('g')
                        .attr('id', d => d.id)
                        .style('outline', 'none')
                        .attr('class', `${containerClass} pointer`)
                        .attr('src-id', d => lodash.objGet(d.source, nodeIdPath))
                        .attr('target-id', d => lodash.objGet(d.target, nodeIdPath))
                    // Bind events
                    Object.keys(events).forEach(key =>
                        linkCtr.on(key, function(e, link) {
                            const linkCtr = d3Select(this)
                            events[key]({ e, link, linkCtr, pathGenerator })
                        })
                    )
                    this.afterJoinProcess({
                        joinType: 'enter',
                        linkCtr,
                        pathGenerator,
                        cb: afterEnter,
                    })
                },
                // update is called when node changes it size or its position
                update =>
                    this.afterJoinProcess({
                        joinType: 'update',
                        linkCtr: update,
                        pathGenerator,
                        cb: afterUpdate,
                    }),
                exit => exit.remove()
            )
    }
    /**
     * Set the event styles of the links based on the specified event type and modified link styles.
     * @param {Array} params.links - Links data
     * @param {string} params.eventType - The type of event to update styles for.
     * @param {Function} [params.evtStylesMod] - The function to return the modified event styles for the link.
     */
    setEventStyles({ links, eventType, evtStylesMod }) {
        const scope = this
        links.forEach(link => {
            if (eventType) {
                link.evtStyles = lodash.merge(
                    Object.keys(scope.config[eventType]).reduce((obj, style) => {
                        obj[style] = scope.getStyle(link, `${eventType}.${style}`)
                        return obj
                    }, {}),
                    t(evtStylesMod).safeFunction(link)
                )
            } else delete link.evtStyles
        })
    }
}
