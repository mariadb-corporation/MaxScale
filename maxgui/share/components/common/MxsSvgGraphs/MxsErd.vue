<template>
    <div class="fill-height er-diagram">
        <div v-if="isDraggingNode" class="dragging-mask" />
        <v-progress-linear v-if="isRendering" indeterminate color="primary" />
        <!-- Graph-board and its child components will be rendered but they won't be visible until
             the simulation is done. This ensures node size can be calculated dynamically.
        -->
        <graph-board
            v-model="panAndZoom"
            :scaleExtent="scaleExtent"
            :style="{ visibility: isRendering ? 'hidden' : 'visible' }"
            :dim="ctrDim"
            :graphDim="graphDim"
            @get-graph-ctr="svgGroup = $event"
        >
            <template v-slot:append="{ data: { transform } }">
                <graph-nodes
                    ref="graphNodes"
                    :nodes="graphData.nodes"
                    :coordMap.sync="graphNodeCoordMap"
                    :style="{ transform }"
                    :defNodeSize="defNodeSize"
                    draggable
                    hoverable
                    :boardZoom="panAndZoom.k"
                    autoWidth
                    @node-size-map="onNodesRendered"
                    @drag="onNodeDrag"
                    @drag-end="onNodeDragEnd"
                    @mouseenter="mouseenterNode"
                    @mouseleave="mouseleaveNode"
                    v-on="$listeners"
                >
                    <template v-slot:default="{ data: { node } }">
                        <table
                            class="entity-table"
                            :style="{ borderColor: node.styles.highlightColor }"
                        >
                            <thead>
                                <tr :style="{ height: `${entitySizeConfig.headerHeight}px` }">
                                    <th
                                        class="text-center font-weight-bold text-no-wrap rounded-tr-lg rounded-tl-lg px-4"
                                        colspan="3"
                                    >
                                        {{ $helpers.unquoteIdentifier(node.data.name) }}
                                    </th>
                                </tr>
                            </thead>
                            <tbody>
                                <tr
                                    v-for="col in node.data.definitions.cols"
                                    :key="`key_${node.id}_${col.name}`"
                                    :style="{
                                        height: `${entitySizeConfig.rowHeight}px`,
                                        ...getHighlightColStyle({ node, colName: col.name }),
                                    }"
                                >
                                    <td>
                                        <erd-key-icon
                                            class="fill-height d-flex align-center"
                                            :data="getKeyIcon({ node, colName: col.name })"
                                        />
                                    </td>
                                    <td>
                                        <div class="fill-height d-flex align-center">
                                            <mxs-truncate-str
                                                :tooltipItem="{
                                                    txt: $helpers.unquoteIdentifier(col.name),
                                                }"
                                                :maxWidth="tdMaxWidth"
                                            />
                                        </div>
                                    </td>
                                    <td
                                        class="text-end"
                                        :style="{
                                            color:
                                                $typy(
                                                    getHighlightColStyle({
                                                        node,
                                                        colName: col.name,
                                                    }),
                                                    'color'
                                                ).safeString || '#6c7c7b',
                                        }"
                                    >
                                        <div class="fill-height d-flex align-center">
                                            <mxs-truncate-str
                                                :tooltipItem="{
                                                    txt: `${col.data_type}${
                                                        $typy(col, 'data_type_size').isDefined
                                                            ? `(${col.data_type_size})`
                                                            : ''
                                                    }`,
                                                }"
                                                :maxWidth="tdMaxWidth"
                                            />
                                        </div>
                                    </td>
                                </tr>
                            </tbody>
                        </table>
                    </template>
                </graph-nodes>
            </template>
        </graph-board>
    </div>
</template>

<script>
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
/*
 * Emits:
 * - $emit('on-nodes-coords-update', nodes:[])
 * - $emit('dbl-click-node', { e:Event, node: {} })
 */
import {
    forceSimulation,
    forceLink,
    forceManyBody,
    forceCenter,
    forceCollide,
    forceX,
    forceY,
} from 'd3-force'
import { min as d3Min, max as d3Max } from 'd3-array'
import GraphBoard from '@share/components/common/MxsSvgGraphs/GraphBoard.vue'
import GraphNodes from '@share/components/common/MxsSvgGraphs/GraphNodes.vue'
import GraphConfig from '@share/components/common/MxsSvgGraphs/GraphConfig'
import EntityLink from '@share/components/common/MxsSvgGraphs/EntityLink'
import ErdKeyIcon from '@share/components/common/MxsSvgGraphs/ErdKeyIcon'
import { EVENT_TYPES, LINK_SHAPES } from '@share/components/common/MxsSvgGraphs/config'
import tokens from '@wsSrc/utils/createTableTokens'

export default {
    name: 'mxs-erd',
    components: {
        'graph-board': GraphBoard,
        'graph-nodes': GraphNodes,
        'erd-key-icon': ErdKeyIcon,
    },
    props: {
        ctrDim: { type: Object, required: true },
        data: { type: Object, required: true },
        graphConfigData: { type: Object, required: true },
        isLaidOut: { type: Boolean, default: false },
    },
    data() {
        return {
            isRendering: false,
            svgGroup: null,
            graphNodeCoordMap: {},
            graphData: { nodes: [], links: [] },
            simulation: null,
            defNodeSize: { width: 250, height: 100 },
            chosenLinks: [],
            entityLink: null,
            graphConfig: null,
            isDraggingNode: false,
            graphDim: {},
            panAndZoom: { x: 0, y: 0, k: 1 },
        }
    },
    computed: {
        scaleExtent() {
            return [0.25, 2]
        },
        tdMaxWidth() {
            // entity max-width / 2 - offset. Offset includes padding and border
            return 320 / 2 - 27
        },
        entityKeyMap() {
            return this.graphData.nodes.reduce((map, node) => {
                map[node.id] = node.data.definitions.keys
                return map
            }, {})
        },
        entitySizeConfig() {
            return this.graphConfigData.linkShape.entitySizeConfig
        },
        highlightColStyleMap() {
            return this.chosenLinks.reduce((map, link) => {
                const {
                    source,
                    target,
                    relationshipData: { source_attr, target_attr },
                    styles: { invisibleHighlightColor },
                } = link

                if (!map[source.id]) map[source.id] = []
                if (!map[target.id]) map[target.id] = []
                const style = {
                    backgroundColor: invisibleHighlightColor,
                    color: 'white',
                }
                map[source.id].push({ col: source_attr, ...style })
                map[target.id].push({ col: target_attr, ...style })
                return map
            }, {})
        },
        isAttrToAttr() {
            return this.$typy(this.graphConfigData, 'link.isAttrToAttr').safeBoolean
        },
        isStraightShape() {
            return (
                this.$typy(this.graphConfigData, 'linkShape.type').safeString ===
                LINK_SHAPES.STRAIGHT
            )
        },
        globalLinkColor() {
            return this.$typy(this.graphConfigData, 'link.color').safeString
        },
        nodeIds() {
            return this.$typy(this.data, 'nodes').safeArray.map(n => n.id)
        },
    },
    created() {
        this.initGraphConfig()
        this.init()
        this.graphDim = this.ctrDim
    },
    activated() {
        this.watchNodeIds()
        this.watchCtrHeight()
    },
    deactivated() {
        this.$typy(this.unwatch_nodeIds).safeFunction()
        this.$typy(this.unwatch_ctrHeight).safeFunction()
    },
    beforeDestroy() {
        this.$typy(this.unwatch_nodeIds).safeFunction()
        this.$typy(this.unwatch_ctrHeight).safeFunction()
        this.$typy(this.unwatch_graphConfigData).safeFunction()
    },
    methods: {
        watchNodeIds() {
            this.unwatch_nodeIds = this.$watch(
                'nodeIds',
                (v, oV) => {
                    if (!this.$helpers.lodash.isEqual(v, oV)) this.init()
                },
                { deep: true }
            )
        },
        watchCtrHeight() {
            this.unwatch_ctrHeight = this.$watch('ctrDim.height', v => {
                if (v) this.fitIntoView()
            })
        },
        /**
         * Call this function will trigger rerender the graph
         * D3 mutates data, this method deep clones data leaving the original intact.
         */
        init() {
            if (this.$typy(this.data, 'nodes').safeArray.length) {
                this.isRendering = true
                this.assignData()
                this.handleFilterCompositeKeys(this.isAttrToAttr)
            }
        },
        assignData() {
            this.graphData = this.$helpers.lodash.cloneDeep(this.data)
        },
        handleFilterCompositeKeys(v) {
            this.graphData.links.forEach(link => {
                if (link.isPartOfCompositeKey) link.hidden = !v
            })
        },
        /**
         *
         * @param {Object} nodeSizeMap - size of nodes
         */
        onNodesRendered(nodeSizeMap) {
            this.graphData.nodes.forEach(node => {
                node.size = nodeSizeMap[node.id]
            })
            if (Object.keys(nodeSizeMap).length) this.runSimulation()
        },
        runSimulation() {
            this.simulation = forceSimulation(this.graphData.nodes)
                .force(
                    'link',
                    forceLink(this.graphData.links).id(d => d.id)
                )
                .force(
                    'charge',
                    forceManyBody()
                        .strength(-15)
                        .theta(0.5)
                )
                .force('center', forceCenter(this.ctrDim.width / 2, this.ctrDim.height / 2))
                .force('x', forceX().strength(0.1))
                .force('y', forceY().strength(0.1))

            if (this.isLaidOut) {
                this.simulation.stop()
                // Adding a loading animation can enhance the smoothness, even if the graph is already laid out.
                this.$helpers.delay(300).then(this.draw)
            } else {
                this.simulation.alphaMin(0.1).on('end', this.draw)
                this.handleCollision()
            }
        },
        initGraphConfig() {
            this.graphConfig = new GraphConfig(this.graphConfigData)
        },
        initLinkInstance() {
            this.entityLink = new EntityLink(this.graphConfig)
            this.watchConfig()
        },
        draw() {
            this.setGraphNodeCoordMap()
            this.initLinkInstance()
            this.drawLinks()
            this.fitIntoView()
            this.isRendering = false
            this.onNodesCoordsUpdate()
        },
        setGraphNodeCoordMap() {
            this.graphNodeCoordMap = this.graphData.nodes.reduce((map, n) => {
                const { x, y, id } = n
                if (id) map[id] = { x, y }
                return map
            }, {})
        },
        getLinks() {
            return this.simulation.force('link').links()
        },
        drawLinks() {
            this.entityLink.draw({
                containerEle: this.svgGroup,
                data: this.getLinks().filter(link => !link.hidden),
            })
        },
        handleCollision() {
            this.simulation.force(
                'collide',
                forceCollide().radius(d => {
                    const { width, height } = d.size
                    // Because nodes are densely packed,  this adds an extra radius of 100 pixels to the nodes
                    return Math.sqrt(width * width + height * height) / 2 + 100
                })
            )
        },
        setChosenLinks(node) {
            this.chosenLinks = this.getLinks().filter(
                d => d.source.id === node.id || d.target.id === node.id
            )
        },
        setEventLinkStyles(eventType) {
            this.entityLink.setEventStyles({
                links: this.chosenLinks,
                eventType,
                evtStylesMod: () => (this.isStraightShape ? { color: this.globalLinkColor } : null),
            })
            this.drawLinks()
        },
        onNodeDrag({ node, diffX, diffY }) {
            const nodeData = this.graphData.nodes.find(n => n.id === node.id)
            nodeData.x = nodeData.x + diffX
            nodeData.y = nodeData.y + diffY
            this.setChosenLinks(node)
            if (!this.isDraggingNode) this.setEventLinkStyles(EVENT_TYPES.DRAGGING)
            this.isDraggingNode = true
            /**
             * drawLinks is called inside setEventLinkStyles method but it run once.
             * To ensure that the paths of links continue to be redrawn, call it again while
             * dragging the node
             */
            this.drawLinks()
        },
        onNodeDragEnd() {
            if (this.isDraggingNode) {
                this.setEventLinkStyles(EVENT_TYPES.NONE)
                this.isDraggingNode = false
                this.chosenLinks = []
                this.onNodesCoordsUpdate()
            }
        },
        mouseenterNode({ node }) {
            this.setChosenLinks(node)
            this.setEventLinkStyles(EVENT_TYPES.HOVER)
        },
        mouseleaveNode() {
            this.setEventLinkStyles(EVENT_TYPES.NONE)
            this.chosenLinks = []
        },
        findKeyTypeByColName({ node, colName }) {
            const nodeKeys = this.entityKeyMap[node.id]
            const keyTypes = [tokens.primaryKey, tokens.uniqueKey, tokens.key]
            return keyTypes.find(type =>
                this.$typy(nodeKeys, `[${type}]`).safeArray.some(key =>
                    key.index_cols.some(item => item.name === colName)
                )
            )
        },
        getKeyIcon({ node, colName }) {
            const keyType = this.findKeyTypeByColName({ node, colName })
            const { color } = this.getHighlightColStyle({ node, colName }) || {}
            switch (keyType) {
                case tokens.primaryKey:
                    return {
                        icon: 'mdi-key-variant',
                        color: color ? color : 'primary',
                        style: {
                            transform: 'rotate(180deg) scale(1, -1)',
                        },
                        size: 18,
                    }
                case tokens.uniqueKey:
                    return {
                        icon: '$vuetify.icons.mxs_uniqueIndexKey',
                        color: color ? color : 'navigation',
                        size: 16,
                    }
                case tokens.key:
                    return {
                        icon: '$vuetify.icons.mxs_indexKey',
                        color: color ? color : 'navigation',
                        size: 16,
                    }
            }
        },
        getHighlightColStyle({ node, colName }) {
            const cols = this.highlightColStyleMap[node.id] || []
            return cols.find(item => item.col === colName)
        },
        watchConfig() {
            this.unwatch_graphConfigData = this.$watch(
                'graphConfigData',
                (v, oV) => {
                    /**
                     * Because only one attribute can be changed at a time, so it's safe to
                     * access the diff with hard-code indexes.
                     */
                    const diff = this.$typy(this.$helpers.deepDiff(oV, v), '[0]').safeObjectOrEmpty
                    const diffKey = diff.path[1]
                    const aValueDiff = this.$helpers.lodash.objGet(v, diff.path.join('.'))
                    this.graphConfig.updateConfig({
                        key: diff.path[0],
                        patch: { [diffKey]: aValueDiff },
                    })
                    switch (diffKey) {
                        case 'isAttrToAttr':
                            this.handleIsAttrToAttrMode(aValueDiff)
                            break
                    }
                    this.drawLinks()
                },
                { deep: true }
            )
        },
        /**
         * If value is true, the diagram shows all links including composite links for composite keys
         * @param {boolean} v
         */
        handleIsAttrToAttrMode(v) {
            this.handleFilterCompositeKeys(v)
            this.simulation.force('link').links(this.graphData.links)
        },
        /**
         * Call this function when node coordinates are updated.
         */
        onNodesCoordsUpdate() {
            this.$emit(
                'on-nodes-coords-update',
                this.$helpers.lodash.cloneDeep(this.graphData.nodes)
            )
        },
        /**
         * TODO: Add a mode to zoom in a particular node
         * Auto adjust (zoom in or out) the contents of a graph to fit within the view.
         */
        fitIntoView() {
            // set up zoom transform:
            const minX = d3Min(this.graphData.nodes, n => n.x - n.size.width / 2)
            const minY = d3Min(this.graphData.nodes, n => n.y - n.size.height / 2)
            const maxX = d3Max(this.graphData.nodes, n => n.x + n.size.width / 2)
            const maxY = d3Max(this.graphData.nodes, n => n.y + n.size.height / 2)
            const graphWidth = maxX - minX
            const graphHeight = maxY - minY

            // scales with 2% padding
            const xScale = (this.ctrDim.width / graphWidth) * 0.98
            const yScale = (this.ctrDim.height / graphHeight) * 0.98

            // get most restrictive scale and adjust value to fit within scaleExtent
            let k = Math.min(xScale, yScale, this.scaleExtent[1])
            if (k < this.scaleExtent[0]) k = this.scaleExtent[0]

            if (k === 1) return null

            const x = this.ctrDim.width / 2 - ((minX + maxX) / 2) * k
            const y = this.ctrDim.height / 2 - ((minY + maxY) / 2) * k
            this.panAndZoom = { x, y, k }
        },
    },
}
</script>

<style lang="scss" scoped>
.entity-table {
    background: white;
    width: 100%;
    border-spacing: 0px;
    thead {
        th {
            border-top: 8px solid;
            border-right: 1px solid;
            border-bottom: 1px solid;
            border-left: 1px solid;
            border-color: inherit;
        }
    }
    tbody {
        tr {
            &:hover {
                background: $tr-hovered-color;
            }
            td {
                white-space: nowrap;
                padding: 0px 8px;
                &:first-of-type {
                    padding-left: 8px;
                    padding-right: 0px;
                    border-left: 1px solid;
                    border-color: inherit;
                }
                &:nth-of-type(2) {
                    padding-left: 2px;
                }
                &:last-of-type {
                    border-right: 1px solid;
                    border-color: inherit;
                }
            }
            &:last-of-type {
                td {
                    border-bottom: 1px solid;
                    border-color: inherit;
                    &:first-of-type {
                        border-bottom-left-radius: 8px !important;
                    }
                    &:last-of-type {
                        border-bottom-right-radius: 8px !important;
                    }
                }
            }
        }
    }
}
</style>
