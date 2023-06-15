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
                    :nodes="graphData.nodes"
                    :coordMap.sync="graphNodeCoordMap"
                    :style="style"
                    :defNodeSize="defNodeSize"
                    :nodeStyle="{ userSelect: 'none' }"
                    draggable
                    hoverable
                    :boardZoom="panAndZoomData.k"
                    autoWidth
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
                            @dblclick.stop="
                                $emit('on-choose-node-opt', { type: ENTITY_OPT_TYPES.ALTER, node })
                            "
                        >
                            <thead>
                                <tr :style="{ height: `${entitySizeConfig.headerHeight}px` }">
                                    <th
                                        class="text-center font-weight-bold text-no-wrap rounded-tr-lg rounded-tl-lg px-4"
                                        colspan="3"
                                    >
                                        <div class="d-flex flex-row align-center justify-center">
                                            <div class="flex-grow-1">
                                                {{ node.data.options.name }}
                                            </div>
                                            <v-btn
                                                :id="`setting-btn-${node.id}`"
                                                x-small
                                                class="setting-btn"
                                                :class="{
                                                    'setting-btn--visible':
                                                        activeNodeMenuId === node.id,
                                                }"
                                                icon
                                                color="primary"
                                                @click.stop="activeNodeMenu = node"
                                            >
                                                <v-icon size="14">
                                                    $vuetify.icons.mxs_settings
                                                </v-icon>
                                            </v-btn>
                                        </div>
                                    </th>
                                </tr>
                            </thead>
                            <tbody>
                                <tr
                                    v-for="col in node.data.definitions.cols"
                                    :key="col[COL_ATTR_IDX_MAP[COL_ATTRS.ID]]"
                                    :style="{
                                        height: `${entitySizeConfig.rowHeight}px`,
                                        ...getHighlightColStyle({ node, colName: getColName(col) }),
                                    }"
                                >
                                    <td>
                                        <erd-key-icon
                                            class="fill-height d-flex align-center"
                                            :data="getKeyIcon({ node, colName: getColName(col) })"
                                        />
                                    </td>
                                    <td>
                                        <div class="fill-height d-flex align-center">
                                            <mxs-truncate-str
                                                :tooltipItem="{ txt: getColName(col) }"
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
                                                        colName: getColName(col),
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
        <v-menu
            v-if="activeNodeMenuId"
            :key="`#setting-btn-${activeNodeMenuId}`"
            :value="activeNodeMenuId"
            transition="slide-y-transition"
            offset-y
            left
            content-class="v-menu--mariadb v-menu--mariadb-with-shadow-no-border"
            :activator="`#setting-btn-${activeNodeMenuId}`"
            @input="onCloseNodeMenu"
        >
            <v-list>
                <v-list-item
                    v-for="(opt, i) in existingEntityOpts"
                    :key="i"
                    dense
                    link
                    class="px-2"
                    @click="handleChooseOpt(opt)"
                >
                    <v-list-item-title class="mxs-color-helper text-text">
                        <div class="d-inline-block text-center mr-2" style="width:22px">
                            <v-icon v-if="opt.icon" :color="opt.color" :size="opt.iconSize">
                                {{ opt.icon }}
                            </v-icon>
                        </div>
                        {{ opt.text }}
                    </v-list-item-title>
                </v-list-item>
            </v-list>
        </v-menu>
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
 * - $emit('on-rendered')
 * - $emit('on-nodes-coords-update', nodes:[])
 * - $emit('on-choose-node-opt', { type:string, node:object })
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
import GraphConfig from '@share/components/common/MxsSvgGraphs/GraphConfig'
import EntityLink from '@wsSrc/components/worksheets/ErdWke/EntityLink'
import ErdKeyIcon from '@wsSrc/components/worksheets/ErdWke/ErdKeyIcon'
import { EVENT_TYPES } from '@share/components/common/MxsSvgGraphs/linkConfig'
import { getConfig, LINK_SHAPES } from '@wsSrc/components/worksheets/ErdWke/config'
import queryHelper from '@wsSrc/store/queryHelper'

export default {
    name: 'entity-diagram',
    components: {
        'erd-key-icon': ErdKeyIcon,
    },
    props: {
        dim: { type: Object, required: true },
        panAndZoom: { type: Object, required: true }, // sync
        scaleExtent: { type: Array, required: true },
        data: { type: Object, required: true },
        graphConfigData: { type: Object, required: true },
        isLaidOut: { type: Boolean, default: false },
        activeNodeId: { type: String, default: '' },
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
            activeNodeMenu: null,
        }
    },
    computed: {
        ...mapState({
            CREATE_TBL_TOKENS: state => state.mxsWorkspace.config.CREATE_TBL_TOKENS,
            COL_ATTRS: state => state.mxsWorkspace.config.COL_ATTRS,
            COL_ATTR_IDX_MAP: state => state.mxsWorkspace.config.COL_ATTR_IDX_MAP,
            ENTITY_OPT_TYPES: state => state.mxsWorkspace.config.ENTITY_OPT_TYPES,
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
        // Options for existing entities
        existingEntityOpts() {
            return [
                {
                    text: this.$mxs_t('alterTbl'),
                    type: this.ENTITY_OPT_TYPES.ALTER,
                    icon: '$vuetify.icons.mxs_edit',
                    iconSize: 16,
                    color: 'primary',
                },
                {
                    text: this.$mxs_t('dropTbl'),
                    type: this.ENTITY_OPT_TYPES.DROP,
                    icon: '$vuetify.icons.mxs_delete',
                    iconSize: 16,
                    color: 'error',
                },
            ]
        },
        activeNodeMenuId() {
            return this.$typy(this.activeNodeMenu, 'id').safeString
        },
    },
    created() {
        this.initGraphConfig()
        this.init()
        this.graphDim = this.dim
    },
    beforeDestroy() {
        this.$typy(this.unwatch_graphConfigData).safeFunction()
    },
    methods: {
        /**
         * Render the graph with a loading progress indicator
         */
        init() {
            if (this.$typy(this.data, 'nodes').safeArray.length) {
                this.isRendering = true
                this.assignData()
            }
        },
        /**
         * @public
         * Call this method to update the data of a node
         */
        updateNode({ id, data }) {
            const index = this.graphData.nodes.findIndex(node => node.id === id)
            if (index >= 0) {
                this.$set(this.graphData.nodes[index], 'data', data)
                // Re-calculate the size
                this.$refs.graphNodes.onNodeResized(id)
            }
        },
        /**
         * @public
         * Call this method to add a new node
         */
        addNode(node) {
            this.graphData.nodes.push(node)
        },
        /**
         * D3 mutates data, this method deep clones data leaving the original intact
         * and call handleFilterCompositeKeys to handle composite keys case.
         * By assigning `data` to `graphData`,  @node-size-map event will be
         * emitted from `mxs-svg-graph-nodes` component which triggers the drawing
         * of the graph if there is a change in the ID of the nodes.
         */
        assignData() {
            const data = this.$helpers.lodash.cloneDeep(this.data)
            this.graphData = {
                nodes: this.$typy(data, 'nodes').safeArray,
                links: this.$typy(data, 'links').safeArray,
            }
            this.handleFilterCompositeKeys(this.isAttrToAttr)
        },
        handleFilterCompositeKeys(v) {
            this.graphData.links.forEach(link => {
                if (link.isPartOfCompositeKey) link.hidden = !v
            })
        },
        /**
         * @param {Object} nodeSizeMap - size of nodes
         */
        updateNodeSizes(nodeSizeMap) {
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
        draw() {
            this.setGraphNodeCoordMap()
            this.initLinkInstance()
            this.drawLinks()
            this.onNodesCoordsUpdate()
            this.isRendering = false
            this.$emit('on-rendered')
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
        getKeyIcon({ node, colName }) {
            const keyType = queryHelper.findKeyTypeByColName({
                keys: this.entityKeyMap[node.id],
                colName,
            })
            const { color } = this.getHighlightColStyle({ node, colName }) || {}
            switch (keyType) {
                case this.CREATE_TBL_TOKENS.primaryKey:
                    return {
                        icon: 'mdi-key-variant',
                        color: color ? color : 'primary',
                        style: {
                            transform: 'rotate(180deg) scale(1, -1)',
                        },
                        size: 18,
                    }
                case this.CREATE_TBL_TOKENS.uniqueKey:
                    return {
                        icon: '$vuetify.icons.mxs_uniqueIndexKey',
                        color: color ? color : 'navigation',
                        size: 16,
                    }
                case this.CREATE_TBL_TOKENS.key:
                    return {
                        icon: '$vuetify.icons.mxs_indexKey',
                        color: color ? color : 'navigation',
                        size: 16,
                    }
            }
        },
        getColName(col) {
            return col[this.COL_ATTR_IDX_MAP[this.COL_ATTRS.NAME]]
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
        onCloseNodeMenu() {
            this.activeNodeMenu = null
        },
        handleChooseOpt(opt) {
            this.$emit('on-choose-node-opt', { type: opt.type, node: this.activeNodeMenu })
            this.onCloseNodeMenu()
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
            .setting-btn {
                visibility: hidden;
                &--visible {
                    visibility: visible;
                }
            }
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
    &:hover {
        thead th {
            .setting-btn {
                visibility: visible;
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
}
</style>
