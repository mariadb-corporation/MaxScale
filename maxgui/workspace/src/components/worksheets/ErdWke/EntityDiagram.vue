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
            @get-graph-ctr="linkContainer = $event"
            v-on="$listeners"
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
                    :hoverable="hoverable"
                    :boardZoom="panAndZoomData.k"
                    autoWidth
                    dblclick
                    contextmenu
                    click
                    :clickOutside="clickOutside"
                    :clickedNodeId.sync="clickedNodeId"
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
                        <ref-points
                            v-if="node.id === clickedNodeId"
                            :node="node"
                            :entitySizeConfig="entitySizeConfig"
                            :linkContainer="linkContainer"
                            :boardZoom="panAndZoomData.k"
                            :graphConfig="graphConfig.config"
                            @drawing="onDrawingFk"
                            @draw-end="onEndDrawFk"
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
                                            </div>
                                            <slot name="entity-setting-btn" :node="node" />
                                        </div>
                                    </th>
                                </tr>
                            </thead>
                            <tbody>
                                <tr
                                    v-for="(col, colId) in node.data.defs.col_map"
                                    :key="colId"
                                    :style="{
                                        height: `${entitySizeConfig.rowHeight}px`,
                                        ...getHighlightColStyle({ node, colId }),
                                    }"
                                    v-on="
                                        isDrawingFk
                                            ? {
                                                  mouseenter: () => setRefTargetData({ node, col }),
                                                  mouseleave: () => (refTarget = null),
                                              }
                                            : {}
                                    "
                                >
                                    <td>
                                        <erd-key-icon
                                            class="fill-height d-flex align-center"
                                            :data="getKeyIcon({ node, colId })"
                                        />
                                    </td>
                                    <td>
                                        <div class="fill-height d-flex align-center">
                                            <mxs-truncate-str
                                                :tooltipItem="{ txt: col.name }"
                                                :maxWidth="tdMaxWidth"
                                            />
                                        </div>
                                    </td>
                                    <td
                                        class="text-end"
                                        :style="{
                                            color:
                                                $typy(
                                                    getHighlightColStyle({ node, colId }),
                                                    'color'
                                                ).safeString || '#6c7c7b',
                                        }"
                                    >
                                        <div class="fill-height d-flex align-center text-lowercase">
                                            <mxs-truncate-str
                                                :tooltipItem="{ txt: col.data_type }"
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
        <v-tooltip
            v-if="hoveredFkId"
            :key="hoveredFkId"
            bottom
            transition="slide-y-transition"
            :activator="`#${hoveredFkId}`"
            absolute
            offset-y
            :position-x="tooltipX"
            :position-y="tooltipY"
            :open-delay="300"
        >
            <pre>{{ hoveredFkInfo }}</pre>
        </v-tooltip>
    </div>
</template>

<script>
/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
/*
 * Emits:
 * - on-rendered({ nodes:array, links:array })
 * - on-node-drag-end(node)
 * - on-create-new-fk({ node:object, currentFkMap: object, newKey: object, refNode: object, })
 * - on-node-contextmenu({e: Event, node:object})
 * - on-link-contextmenu({e: Event, link:object})
 */
import { forceSimulation, forceLink, forceCenter, forceCollide, forceX, forceY } from 'd3-force'
import { min as d3Min, max as d3Max } from 'd3-array'
import GraphConfig from '@share/components/common/MxsSvgGraphs/GraphConfig'
import EntityLink from '@wsSrc/components/worksheets/ErdWke/EntityLink'
import ErdKeyIcon from '@wsSrc/components/worksheets/ErdWke/ErdKeyIcon'
import RefPoints from '@wsSrc/components/worksheets/ErdWke/RefPoints'
import { EVENT_TYPES } from '@share/components/common/MxsSvgGraphs/linkConfig'
import { LINK_SHAPES } from '@share/components/common/MxsSvgGraphs/shapeConfig'
import { getConfig } from '@wsSrc/components/worksheets/ErdWke/config'
import erdHelper from '@wsSrc/utils/erdHelper'
import html2canvas from 'html2canvas'
import { CREATE_TBL_TOKENS, REF_OPTS } from '@wsSrc/constants'

export default {
    name: 'entity-diagram',
    components: {
        'erd-key-icon': ErdKeyIcon,
        RefPoints,
    },
    props: {
        dim: { type: Object, required: true },
        panAndZoom: { type: Object, required: true }, // sync
        scaleExtent: { type: Array, required: true },
        nodes: { type: Array, required: true },
        graphConfigData: { type: Object, required: true },
        isLaidOut: { type: Boolean, default: false },
        activeNodeId: { type: String, default: '' },
        refTargetMap: { type: Object, required: true },
        tablesColNameMap: { type: Object, required: true },
        colKeyCategoryMap: { type: Object, required: true },
    },
    data() {
        return {
            isRendering: false,
            linkContainer: null,
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
            clickedNodeId: '',
            clickOutside: true,
            refTarget: null,
            isDrawingFk: false,
            hoveredLink: null,
            tooltipX: 0,
            tooltipY: 0,
        }
    },
    computed: {
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
        entityKeyCategoryMap() {
            return this.graphNodes.reduce((map, node) => {
                map[node.id] = node.data.defs.key_category_map
                return map
            }, {})
        },
        nodeMap() {
            return this.$helpers.lodash.keyBy(this.graphNodes, 'id')
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
        isHighlightAll() {
            return this.$typy(this.graphConfigData, 'link.isHighlightAll').safeBoolean
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
        hoverable() {
            return Boolean(!this.clickedNodeId)
        },
        hoveredFk() {
            if (this.hoveredLink)
                return this.getFks(this.hoveredLink.source).find(
                    key => key.id === this.hoveredLink.id
                )
            return null
        },
        hoveredFkId() {
            return this.$typy(this.hoveredFk, 'id').safeString
        },
        hoveredFkInfo() {
            if (
                this.hoveredFk &&
                !this.$typy(this.refTargetMap).isEmptyObject &&
                !this.$typy(this.tablesColNameMap).isEmptyObject
            )
                return erdHelper.genConstraint({
                    key: this.hoveredFk,
                    refTargetMap: this.refTargetMap,
                    tablesColNameMap: this.tablesColNameMap,
                    stagingColNameMap: this.$typy(
                        this.tablesColNameMap,
                        `[${this.hoveredLink.source.id}]`
                    ).safeObjectOrEmpty,
                })
            return ''
        },
    },
    watch: {
        hoverable(v) {
            if (!v) this.mouseleaveNode()
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
                const fks = this.getFks(node)
                fks.forEach(fk => {
                    links = [
                        ...links,
                        ...erdHelper.handleGenErdLink({
                            srcNode: node,
                            fk,
                            nodes,
                            isAttrToAttr: this.isAttrToAttr,
                            colKeyCategoryMap: this.colKeyCategoryMap,
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
         * Get the correct dimension of the nodes for controlling the zoom
         */
        getGraphExtent() {
            return {
                minX: d3Min(this.graphNodes, n => n.x - n.size.width / 2) || 0,
                minY: d3Min(this.graphNodes, n => n.y - n.size.height / 2) || 0,
                maxX: d3Max(this.graphNodes, n => n.x + n.size.width / 2) || this.graphDim.width,
                maxY: d3Max(this.graphNodes, n => n.y + n.size.height / 2) || this.graphDim.height,
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
        update(nodes) {
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
        /**
         * @param {function} cb - callback function to be run after the simulation is end
         * @public
         */
        runSimulation(cb) {
            let tickCount = 1
            this.simulation = forceSimulation(this.graphNodes)
                .force(
                    'link',
                    forceLink(this.graphLinks)
                        .id(d => d.id)
                        .strength(0.5)
                        .distance(100)
                )
                .force('center', forceCenter(this.dim.width / 2, this.dim.height / 2))
                .force('x', forceX().strength(0.1))
                .force('y', forceY().strength(0.1))

            if (this.isLaidOut) {
                this.simulation.stop()
                // Adding a loading animation can enhance the smoothness, even if the graph is already laid out.
                this.$helpers.delay(this.isRendering ? 300 : 0).then(() => {
                    this.draw(tickCount)
                    this.emitOnRendered()
                })
            } else {
                /**
                 * TODO: Make alphaMin customizable by the user, the smaller the number is, the better
                 * the layout would be but it also takes longer time.
                 */
                this.simulation
                    .alphaMin(0.1)
                    .on('tick', () => {
                        this.draw(tickCount)
                        tickCount++
                    })
                    .on('end', () => {
                        this.emitOnRendered()
                        this.$typy(cb).safeFunction({
                            nodes: this.graphNodes,
                            links: this.graphLinks,
                        })
                    })
                this.handleCollision()
            }
        },
        handleCollision() {
            this.simulation.force(
                'collide',
                forceCollide().radius(d => {
                    const { width, height } = d.size
                    // Because nodes are densely packed,  this adds an extra radius of 50 pixels to the nodes
                    return Math.sqrt(width * width + height * height) / 2 + 50
                })
            )
        },
        initGraphConfig() {
            this.graphConfig = new GraphConfig(
                this.$helpers.lodash.merge(getConfig(), this.graphConfigData)
            )
        },
        initLinkInstance() {
            this.entityLink = new EntityLink(this.graphConfig.config)
            this.watchConfig()
        },
        emitOnRendered() {
            // after finish rendering, set styles for all links
            if (this.isHighlightAll) this.handleHighlightAllLinks()

            this.$emit('on-rendered', { nodes: this.graphNodes, links: this.graphLinks })
        },
        /**
         * @param {number} tickCount - number of times this function is called.
         * This helps to prevent initLinkInstance from being called repeatedly memory which
         * causes memory leaks. initLinkInstance should be called once
         */
        draw(tickCount) {
            this.setGraphNodeCoordMap()
            if (tickCount === 1) this.initLinkInstance()
            this.drawLinks()
            if (tickCount === 1) this.isRendering = false
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
        setEventStyles({ links, eventType }) {
            this.entityLink.setEventStyles({
                links,
                eventType,
                evtStylesMod: () => (this.isStraightShape ? { color: this.globalLinkColor } : null),
            })
        },
        handleMouseOverOut({ e, link, linkCtr, pathGenerator, eventType }) {
            this.hoveredLink = link
            this.tooltipX = e.clientX
            this.tooltipY = e.clientY
            if (!this.isHighlightAll) {
                if (eventType === EVENT_TYPES.HOVER) this.mouseenterNode({ node: link.source })
                else this.mouseleaveNode()
                this.setEventStyles({ links: [link], eventType })
                this.entityLink.drawPaths({ linkCtr, joinType: 'update', pathGenerator })
                this.entityLink.drawMarkers({ linkCtr, joinType: 'update' })
            }
        },
        openContextMenu(param) {
            const { e, link } = param
            e.preventDefault()
            e.stopPropagation()
            this.$emit('on-link-contextmenu', { e, link })
        },
        drawLinks() {
            this.entityLink.draw({
                containerEle: this.linkContainer,
                data: this.getLinks().filter(link => !link.hidden),
                events: {
                    mouseover: param =>
                        this.handleMouseOverOut.bind(this)({
                            ...param,
                            eventType: EVENT_TYPES.HOVER,
                        }),
                    mouseout: param =>
                        this.handleMouseOverOut.bind(this)({
                            ...param,
                            eventType: EVENT_TYPES.NONE,
                        }),
                    contextmenu: param => this.openContextMenu.bind(this)(param),
                    click: param => this.openContextMenu.bind(this)(param),
                },
            })
        },
        setChosenLinks(node) {
            this.chosenLinks = erdHelper.getNodeLinks({ links: this.getLinks(), node })
        },
        setEventLinkStyles(eventType) {
            this.setEventStyles({ eventType, links: this.chosenLinks })
            this.drawLinks()
        },
        onNodeDrag({ node, diffX, diffY }) {
            const nodeData = this.nodeMap[node.id]
            nodeData.x = nodeData.x + diffX
            nodeData.y = nodeData.y + diffY
            if (!this.isHighlightAll) {
                this.setChosenLinks(node)
                if (!this.isDraggingNode) this.setEventLinkStyles(EVENT_TYPES.DRAGGING)
            }
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
                if (!this.isHighlightAll) {
                    this.setEventLinkStyles(EVENT_TYPES.NONE)
                    this.chosenLinks = []
                }
                this.isDraggingNode = false

                this.$emit('on-node-drag-end', node)
            }
        },
        mouseenterNode({ node }) {
            if (!this.isHighlightAll) {
                this.setChosenLinks(node)
                this.setEventLinkStyles(EVENT_TYPES.HOVER)
            }
        },
        mouseleaveNode() {
            if (!this.isHighlightAll) {
                this.setEventLinkStyles(EVENT_TYPES.NONE)
                this.chosenLinks = []
            }
        },
        getKeyIcon({ node, colId }) {
            const {
                primaryKey,
                uniqueKey,
                key,
                fullTextKey,
                spatialKey,
                foreignKey,
            } = CREATE_TBL_TOKENS

            const { color } = this.getHighlightColStyle({ node, colId }) || {}
            const categories = this.colKeyCategoryMap[colId] || []

            let isUQ = false
            if (categories.includes(uniqueKey)) {
                isUQ = erdHelper.isSingleUQ({
                    keyCategoryMap: this.entityKeyCategoryMap[node.id],
                    colId,
                })
            }

            if (categories.includes(primaryKey))
                return {
                    icon: 'mdi-key',
                    color: color ? color : 'primary',
                    size: 18,
                }
            else if (isUQ)
                return {
                    icon: '$vuetify.icons.mxs_uniqueIndexKey',
                    color: color ? color : 'navigation',
                    size: 16,
                }
            else if ([key, fullTextKey, spatialKey, foreignKey].some(k => categories.includes(k)))
                return {
                    icon: '$vuetify.icons.mxs_indexKey',
                    color: color ? color : 'navigation',
                    size: 16,
                }
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
                    const path = diff.path.join('.')
                    const value = diff.rhs
                    this.graphConfig.updateConfig({ path, value })
                    switch (path) {
                        case 'link.isAttrToAttr':
                            this.handleIsAttrToAttrMode(value)
                            break
                        case 'link.isHighlightAll':
                            this.handleHighlightAllLinks()
                            break
                    }
                    this.drawLinks()
                },
                { deep: true }
            )
        },
        handleHighlightAllLinks() {
            if (this.isHighlightAll) this.chosenLinks = this.graphLinks
            this.setEventLinkStyles(this.isHighlightAll ? EVENT_TYPES.HOVER : EVENT_TYPES.NONE)
            if (!this.isHighlightAll) this.chosenLinks = []
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
        onDrawingFk() {
            this.clickOutside = false
            this.isDrawingFk = true
        },
        getFkMap(node) {
            return this.$typy(
                this.entityKeyCategoryMap,
                `[${node.id}][${CREATE_TBL_TOKENS.foreignKey}]`
            ).safeObjectOrEmpty
        },
        getFks(node) {
            return Object.values(this.getFkMap(node))
        },
        onEndDrawFk({ node, cols }) {
            this.isDrawingFk = false
            if (this.refTarget) {
                const currentFkMap = this.getFkMap(node)
                this.$emit('on-create-new-fk', {
                    node,
                    currentFkMap,
                    newKey: {
                        id: `key_${this.$helpers.uuidv1()}`,
                        name: `${node.data.options.name}_ibfk_${Object.keys(currentFkMap).length}`,
                        cols,
                        ...this.refTarget.data,
                    },
                    refNode: this.refTarget.node,
                })
                this.refTarget = null
                this.clickOutside = true // hide ref-points
            } else this.$helpers.doubleRAF(() => (this.clickOutside = true))
        },
        setRefTargetData({ node, col }) {
            this.refTarget = {
                data: {
                    ref_cols: [{ id: col.id }],
                    ref_tbl_id: node.id,
                    on_delete: REF_OPTS.NO_ACTION,
                    on_update: REF_OPTS.NO_ACTION,
                },
                node,
            }
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
