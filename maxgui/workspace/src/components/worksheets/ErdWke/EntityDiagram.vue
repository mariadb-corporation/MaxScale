<template>
    <div class="fill-height er-diagram">
        <div v-if="isDraggingNode" class="dragging-mask" />
        <v-progress-linear v-if="isRendering" indeterminate color="primary" />
        <!-- Graph-board and its child components will be rendered but they won't be visible until
             the simulation is done. This ensures node size can be calculated dynamically.
        -->
        <mxs-svg-graph-board
            v-model="panAndZoomData"
            :scaleExtent="scaleExtent"
            :style="{ visibility: isRendering ? 'hidden' : 'visible' }"
            :dim="dim"
            :graphDim="graphDim"
            @get-graph-ctr="svgGroup = $event"
        >
            <template v-slot:append="{ data: { style } }">
                <mxs-svg-graph-nodes
                    ref="graphNodes"
                    :nodes="graphNodes"
                    :coordMap.sync="graphNodeCoordMap"
                    :style="style"
                    :defNodeSize="defNodeSize"
                    :nodeStyle="{ userSelect: 'none' }"
                    draggable
                    hoverable
                    :boardZoom="panAndZoomData.k"
                    autoWidth
                    dblclick
                    contextmenu
                    @node-size-map="updateNodeSizes"
                    @drag="onNodeDrag"
                    @drag-end="onNodeDragEnd"
                    @mouseenter="mouseenterNode"
                    @mouseleave="mouseleaveNode"
                    v-on="$listeners"
                >
                    <template v-slot:default="{ data: { node } }">
                        <div
                            v-if="node.id === activeNodeId"
                            class="active-node-border-div absolute rounded-lg"
                        />
                        <table
                            class="entity-table"
                            :style="{ borderColor: node.styles.highlightColor }"
                        >
                            <thead>
                                <tr :style="{ height: `${entitySizeConfig.headerHeight}px` }">
                                    <th
                                        class="text-center font-weight-bold text-no-wrap rounded-tr-lg rounded-tl-lg pl-4 pr-1"
                                        colspan="3"
                                    >
                                        <div class="d-flex flex-row align-center justify-center">
                                            <div class="flex-grow-1">
                                                {{ node.data.options.name }}
                                                <slot name="entity-name-append" :node="node" />
                                            </div>
                                            <slot name="entity-setting-btn" :node="node" />
                                        </div>
                                    </th>
                                </tr>
                            </thead>
                            <tbody>
                                <tr
                                    v-for="col in node.data.definitions.cols"
                                    :key="getColId(col)"
                                    :style="{
                                        height: `${entitySizeConfig.rowHeight}px`,
                                        ...getHighlightColStyle({ node, colId: getColId(col) }),
                                    }"
                                >
                                    <td>
                                        <erd-key-icon
                                            class="fill-height d-flex align-center"
                                            :data="getKeyIcon({ node, colId: getColId(col) })"
                                        />
                                    </td>
                                    <td>
                                        <div class="fill-height d-flex align-center">
                                            <mxs-truncate-str
                                                :tooltipItem="{
                                                    txt: col[COL_ATTR_IDX_MAP[COL_ATTRS.NAME]],
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
                                                        colId: getColId(col),
                                                    }),
                                                    'color'
                                                ).safeString || '#6c7c7b',
                                        }"
                                    >
                                        <div class="fill-height d-flex align-center text-lowercase">
                                            <mxs-truncate-str
                                                :tooltipItem="{
                                                    txt: col[COL_ATTR_IDX_MAP[COL_ATTRS.TYPE]],
                                                }"
                                                :maxWidth="tdMaxWidth"
                                            />
                                        </div>
                                    </td>
                                </tr>
                            </tbody>
                        </table>
                    </template>
                </mxs-svg-graph-nodes>
            </template>
        </mxs-svg-graph-board>
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
 * - $emit('on-rendered', { nodes, links })
 * - $emit('on-node-drag-end', node)
 */
import { mapState } from 'vuex'
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
import GraphConfig from '@share/components/common/MxsSvgGraphs/GraphConfig'
import EntityLink from '@wsSrc/components/worksheets/ErdWke/EntityLink'
import ErdKeyIcon from '@wsSrc/components/worksheets/ErdWke/ErdKeyIcon'
import { EVENT_TYPES } from '@share/components/common/MxsSvgGraphs/linkConfig'
import { getConfig, LINK_SHAPES } from '@wsSrc/components/worksheets/ErdWke/config'
import queryHelper from '@wsSrc/store/queryHelper'
import html2canvas from 'html2canvas'

export default {
    name: 'entity-diagram',
    components: {
        'erd-key-icon': ErdKeyIcon,
    },
    props: {
        dim: { type: Object, required: true },
        panAndZoom: { type: Object, required: true }, // sync
        scaleExtent: { type: Array, required: true },
        nodes: { type: Array, required: true },
        graphConfigData: { type: Object, required: true },
        isLaidOut: { type: Boolean, default: false },
        activeNodeId: { type: String, default: '' },
    },
    data() {
        return {
            isRendering: false,
            svgGroup: null,
            graphNodeCoordMap: {},
            graphNodes: [],
            graphLinks: [],
            simulation: null,
            defNodeSize: { width: 250, height: 100 },
            chosenLinks: [],
            entityLink: null,
            graphConfig: null,
            isDraggingNode: false,
            graphDim: {},
        }
    },
    computed: {
        ...mapState({
            CREATE_TBL_TOKENS: state => state.mxsWorkspace.config.CREATE_TBL_TOKENS,
            COL_ATTRS: state => state.mxsWorkspace.config.COL_ATTRS,
            COL_ATTR_IDX_MAP: state => state.mxsWorkspace.config.COL_ATTR_IDX_MAP,
        }),
        panAndZoomData: {
            get() {
                return this.panAndZoom
            },
            set(v) {
                this.$emit('update:panAndZoom', v)
            },
        },
        tdMaxWidth() {
            // entity max-width / 2 - offset. Offset includes padding and border
            return 320 / 2 - 27
        },
        entityKeyMap() {
            return this.graphNodes.reduce((map, node) => {
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
                    relationshipData: { src_attr_id, target_attr_id },
                    styles: { invisibleHighlightColor },
                } = link

                if (!map[source.id]) map[source.id] = []
                if (!map[target.id]) map[target.id] = []
                const style = {
                    backgroundColor: invisibleHighlightColor,
                    color: 'white',
                }
                map[source.id].push({ col: src_attr_id, ...style })
                map[target.id].push({ col: target_attr_id, ...style })
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
    },
    created() {
        this.initGraphConfig()
        // Render the diagram with a loading progress indicator
        if (this.nodes.length) {
            this.isRendering = true
            this.assignData(this.nodes)
        } else this.emitOnRendered()
        this.graphDim = this.dim
    },
    beforeDestroy() {
        this.$typy(this.unwatch_graphConfigData).safeFunction()
    },
    methods: {
        /**
         * @public
         * D3 mutates nodes which breaks reactivity, so to prevent that,
         * nodes must be cloned.
         * By assigning `nodes` to `graphNodes`,  @node-size-map event will be
         * emitted from `mxs-svg-graph-nodes` component which triggers the drawing
         * of the graph if there is a change in the ID of the nodes.
         * @param {Array} nodes - erd nodes
         */
        assignData(nodes) {
            const allNodes = this.$helpers.lodash.cloneDeep(nodes)
            this.graphNodes = allNodes
            this.genLinks(allNodes)
        },
        genLinks(nodes) {
            this.graphLinks = nodes.reduce((links, node) => {
                const fks = this.$typy(
                    node.data.definitions.keys[this.CREATE_TBL_TOKENS.foreignKey]
                ).safeArray
                fks.forEach(fk => {
                    links = [
                        ...links,
                        ...queryHelper.handleGenErdLink({
                            srcNode: node,
                            fk,
                            nodes,
                            isAttrToAttr: this.isAttrToAttr,
                        }),
                    ]
                })
                return links
            }, [])
        },
        getNodeIdx(id) {
            return this.graphNodes.findIndex(n => n.id === id)
        },
        /**
         * @public
         * Call this method to update the data of a node
         */
        updateNode({ id, data }) {
            const index = this.getNodeIdx(id)
            if (index >= 0) {
                this.$set(this.graphNodes[index], 'data', data)
                // Re-calculate the size
                this.$refs.graphNodes.onNodeResized(id)
                this.genLinks(this.graphNodes)
            }
        },
        /**
         * @public
         * Call this method to add a new node
         */
        addNode(node) {
            this.graphNodes.push(node)
            this.genLinks(this.graphNodes)
        },
        /**
         * @public
         * Call this method to add a new node
         */
        removeNode(node) {
            const index = this.getNodeIdx(node.id)
            if (!index >= 0) this.graphNodes.splice(index, 1)
            this.graphLinks = queryHelper.getExcludedLinks({
                links: this.graphLinks,
                node,
            })
        },
        /**
         * @public
         * Get the correct dimension of the nodes for controlling the zoom
         */
        getGraphExtent() {
            return {
                minX: d3Min(this.graphNodes, n => n.x - n.size.width / 2),
                minY: d3Min(this.graphNodes, n => n.y - n.size.height / 2),
                maxX: d3Max(this.graphNodes, n => n.x + n.size.width / 2),
                maxY: d3Max(this.graphNodes, n => n.y + n.size.height / 2),
            }
        },
        /**
         * @public
         * @returns {Promise<Canvas>}
         */
        async getCanvas() {
            return await html2canvas(this.$el, { logging: false })
        },
        /**
         * @public
         * @param {Array} nodes - erd nodes
         */
        redraw(nodes) {
            this.assignData(nodes)
            // setNodeSizeMap will trigger updateNodeSizes method
            this.$nextTick(() => this.$refs.graphNodes.setNodeSizeMap())
        },
        /**
         * @param {Object} nodeSizeMap - size of nodes
         */
        updateNodeSizes(nodeSizeMap) {
            this.graphNodes.forEach(node => {
                node.size = nodeSizeMap[node.id]
            })
            if (Object.keys(nodeSizeMap).length) this.runSimulation()
        },
        runSimulation() {
            this.simulation = forceSimulation(this.graphNodes)
                .force(
                    'link',
                    forceLink(this.graphLinks).id(d => d.id)
                )
                .force(
                    'charge',
                    forceManyBody()
                        .strength(-15)
                        .theta(0.5)
                )
                .force('center', forceCenter(this.dim.width / 2, this.dim.height / 2))
                .force('x', forceX().strength(0.1))
                .force('y', forceY().strength(0.1))

            if (this.isLaidOut) {
                this.simulation.stop()
                // Adding a loading animation can enhance the smoothness, even if the graph is already laid out.
                this.$helpers.delay(this.isRendering ? 300 : 0).then(this.draw)
            } else {
                this.simulation.alphaMin(0.1).on('end', this.draw)
                this.handleCollision()
            }
        },
        initGraphConfig() {
            this.graphConfig = new GraphConfig(
                this.$helpers.lodash.merge(getConfig(), this.graphConfigData)
            )
        },
        initLinkInstance() {
            this.entityLink = new EntityLink(this.graphConfig)
            this.watchConfig()
        },
        emitOnRendered() {
            this.$emit('on-rendered', { nodes: this.graphNodes, links: this.graphLinks })
        },
        draw() {
            this.setGraphNodeCoordMap()
            this.initLinkInstance()
            this.drawLinks()
            this.isRendering = false
            this.emitOnRendered()
        },
        setGraphNodeCoordMap() {
            this.graphNodeCoordMap = this.graphNodes.reduce((map, n) => {
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
            this.chosenLinks = queryHelper.getNodeLinks({ links: this.getLinks(), node })
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
            const nodeData = this.graphNodes.find(n => n.id === node.id)
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
        onNodeDragEnd({ node }) {
            if (this.isDraggingNode) {
                this.setEventLinkStyles(EVENT_TYPES.NONE)
                this.isDraggingNode = false
                this.chosenLinks = []
                this.$emit('on-node-drag-end', node)
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
        getKeyIcon({ node, colId }) {
            const {
                primaryKey,
                uniqueKey,
                key,
                fullTextKey,
                spatialKey,
                foreignKey,
            } = this.CREATE_TBL_TOKENS
            const { color } = this.getHighlightColStyle({ node, colId }) || {}

            const nodeKeys = this.entityKeyMap[node.id]
            const keyTypes = queryHelper.findKeyTypesByColId({ keys: nodeKeys, colId })

            let isUQ = false
            if (keyTypes.includes(uniqueKey))
                isUQ = queryHelper.isSingleUQ({ keys: nodeKeys, colId })

            if (keyTypes.includes(primaryKey))
                return {
                    icon: 'mdi-key-variant',
                    color: color ? color : 'primary',
                    style: {
                        transform: 'rotate(180deg) scale(1, -1)',
                    },
                    size: 18,
                }
            else if (isUQ)
                return {
                    icon: '$vuetify.icons.mxs_uniqueIndexKey',
                    color: color ? color : 'navigation',
                    size: 16,
                }
            else if ([key, fullTextKey, spatialKey, foreignKey].some(k => keyTypes.includes(k)))
                return {
                    icon: '$vuetify.icons.mxs_indexKey',
                    color: color ? color : 'navigation',
                    size: 16,
                }
        },
        getColId(col) {
            return col[this.COL_ATTR_IDX_MAP[this.COL_ATTRS.ID]]
        },
        getHighlightColStyle({ node, colId }) {
            const cols = this.highlightColStyleMap[node.id] || []
            return cols.find(item => item.col === colId)
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
            this.graphLinks.forEach(link => {
                if (link.isPartOfCompositeKey) link.hidden = !v
            })
            this.simulation.force('link').links(this.graphLinks)
        },
    },
}
</script>

<style lang="scss" scoped>
.entity-table {
    background: white;
    width: 100%;
    border-spacing: 0px;
    tr,
    thead,
    tbody {
        border-color: inherit;
    }
    thead {
        th {
            border-top: 7px solid;
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
.active-node-border-div {
    width: calc(100% + 12px);
    height: calc(100% + 12px);
    left: -6px;
    top: -6px;
    border: 4px solid $primary;
    z-index: -1;
    opacity: 0.5;
}
</style>
