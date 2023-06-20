<template>
    <div class="fill-height d-flex flex-column">
        <er-toolbar-ctr
            v-model="graphConfigData"
            :height="toolbarHeight"
            :zoom="panAndZoom.k"
            :isFitIntoView="isFitIntoView"
            @set-zoom="setZoom"
            @on-create-table="handleCreateTable"
        />
        <entity-diagram
            v-if="diagramKey"
            ref="diagram"
            :key="diagramKey"
            :panAndZoom.sync="panAndZoom"
            :data="stagingGraphData"
            :dim="diagramDim"
            :scaleExtent="scaleExtent"
            :graphConfigData="graphConfigData"
            :isLaidOut="isLaidOut"
            :activeNodeId="activeEntityId"
            class="entity-diagram"
            @on-rendered.once="fitIntoView"
            @on-nodes-coords-update="onNodesCoordsUpdate"
            @dblclick="handleDblClickNode"
            @contextmenu="activeNodeMenu = $event"
        >
            <template v-slot:entity-setting-btn="{ node }">
                <v-btn
                    :id="`setting-btn-${node.id}`"
                    x-small
                    class="setting-btn"
                    :class="{
                        'setting-btn--visible': activeNodeMenuId === node.id,
                    }"
                    icon
                    color="primary"
                    @click.stop="activeNodeMenu = node"
                >
                    <v-icon size="14">
                        $vuetify.icons.mxs_settings
                    </v-icon>
                </v-btn>
            </template>
        </entity-diagram>
        <v-menu
            v-if="activeNodeMenuId"
            :key="`#setting-btn-${activeNodeMenuId}`"
            :value="activeNodeMenuId"
            transition="slide-y-transition"
            offset-y
            left
            content-class="v-menu--mariadb v-menu--mariadb-with-shadow-no-border"
            :activator="`#setting-btn-${activeNodeMenuId}`"
            @input="activeNodeMenu = null"
        >
            <v-list>
                <v-list-item
                    v-for="(opt, i) in entityOpts"
                    :key="i"
                    dense
                    link
                    class="px-2"
                    @click="handleChooseNodeOpt({ type: opt.type, node: activeNodeMenu })"
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
 * Change Date: 2027-03-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapMutations, mapState } from 'vuex'
import ErdTask from '@wsModels/ErdTask'
import ErdTaskTmp from '@wsModels/ErdTaskTmp'
import QueryConn from '@wsModels/QueryConn'
import ErToolbarCtr from '@wkeComps/ErdWke/ErToolbarCtr.vue'
import EntityDiagram from '@wsSrc/components/worksheets/ErdWke/EntityDiagram.vue'
import { EventBus } from '@wkeComps/EventBus'
import { LINK_SHAPES } from '@wsSrc/components/worksheets/ErdWke/config'
import { EVENT_TYPES } from '@share/components/common/MxsSvgGraphs/linkConfig'
import { min as d3Min, max as d3Max } from 'd3-array'
import tableTemplate from '@wkeComps/ErdWke/tableTemplate'
import queryHelper from '@wsSrc/store/queryHelper'

export default {
    name: 'diagram-ctr',
    components: { ErToolbarCtr, EntityDiagram },
    props: {
        dim: { type: Object, required: true },
    },
    data() {
        return {
            graphConfigData: {
                link: {
                    color: '#424f62',
                    strokeWidth: 1,
                    isAttrToAttr: false,
                    opacity: 1,
                    [EVENT_TYPES.HOVER]: { color: 'white', invisibleOpacity: 1 },
                    [EVENT_TYPES.DRAGGING]: { color: 'white', invisibleOpacity: 1 },
                },
                marker: { width: 18 },
                linkShape: {
                    type: LINK_SHAPES.ORTHO,
                    entitySizeConfig: { rowHeight: 32, rowOffset: 4, headerHeight: 32 },
                },
            },
            isFitIntoView: false,
            panAndZoom: { x: 0, y: 0, k: 1 },
            diagramKey: '',
            activeNodeMenu: null,
        }
    },
    computed: {
        ...mapState({
            charset_collation_map: state => state.editorsMem.charset_collation_map,
            ENTITY_OPT_TYPES: state => state.mxsWorkspace.config.ENTITY_OPT_TYPES,
        }),
        activeRecord() {
            return ErdTask.getters('activeRecord')
        },
        activeTaskId() {
            return ErdTask.getters('activeRecordId')
        },
        activeErdConn() {
            return QueryConn.getters('activeErdConn')
        },
        graphData() {
            return this.$typy(this.activeRecord, 'data').safeObjectOrEmpty
        },
        stagingGraphData() {
            return ErdTask.getters('stagingGraphData')
        },
        initialNodes() {
            return ErdTask.getters('initialNodes')
        },
        stagingNodes() {
            return ErdTask.getters('stagingNodes')
        },
        activeGraphConfig() {
            return this.$typy(this.activeRecord, 'graph_config').safeObjectOrEmpty
        },
        isLaidOut() {
            return this.$typy(this.activeRecord, 'is_laid_out').safeBoolean
        },
        toolbarHeight() {
            return 40
        },
        diagramDim() {
            return { width: this.dim.width, height: this.dim.height - this.toolbarHeight }
        },
        scaleExtent() {
            return [0.25, 2]
        },
        activeEntityId() {
            return ErdTask.getters('activeEntityId')
        },
        erdTaskKey() {
            return this.$typy(ErdTask.getters('activeTmpRecord'), 'key').safeString
        },
        entityOpts() {
            const isNew = this.isNewEntity(this.activeNodeMenuId)
            const { ALTER, EDIT, DELETE } = this.ENTITY_OPT_TYPES
            return [
                {
                    text: this.$mxs_t(isNew ? 'edit' : 'alter'),
                    type: isNew ? EDIT : ALTER,
                    icon: isNew ? '$vuetify.icons.mxs_edit' : 'mdi-table-edit',
                    iconSize: isNew ? 16 : 20,
                    color: 'primary',
                },
                {
                    text: this.$mxs_t('delete'),
                    type: DELETE,
                    icon: '$vuetify.icons.mxs_delete',
                    iconSize: 16,
                    color: 'error',
                },
            ]
        },
        activeNodeMenuId() {
            return this.$typy(this.activeNodeMenu, 'id').safeString
        },
        eventBus() {
            return EventBus
        },
    },
    watch: {
        graphConfigData: {
            deep: true,
            handler(v) {
                ErdTask.update({
                    where: this.activeTaskId,
                    data: {
                        graph_config: this.$helpers.immutableUpdate(this.activeGraphConfig, {
                            link: {
                                isAttrToAttr: { $set: v.link.isAttrToAttr },
                            },
                            linkShape: {
                                type: { $set: v.linkShape.type },
                            },
                        }),
                    },
                })
            },
        },
        panAndZoom: {
            deep: true,
            handler(v) {
                if (v.eventType && v.eventType == 'wheel') this.isFitIntoView = false
            },
        },
    },
    created() {
        this.graphConfigData = this.$helpers.lodash.merge(
            this.graphConfigData,
            this.activeGraphConfig
        )
    },
    activated() {
        this.watchActiveEntityId()
        this.watchErdTaskKey()
        this.eventBus.$on('entity-editor-ctr-update-node-data', this.updateNode)
    },
    deactivated() {
        this.$typy(this.unwatch_activeEntityId).safeFunction()
        this.$typy(this.unwatch_erdTaskKey).safeFunction()
        this.eventBus.$off('entity-editor-ctr-update-node-data')
    },
    beforeDestroy() {
        this.$typy(this.unwatch_activeEntityId).safeFunction()
        this.$typy(this.unwatch_erdTaskKey).safeFunction()
    },
    methods: {
        ...mapMutations({ SET_SNACK_BAR_MESSAGE: 'mxsApp/SET_SNACK_BAR_MESSAGE' }),
        watchActiveEntityId() {
            this.unwatch_activeEntityId = this.$watch('activeEntityId', v => {
                if (!v) this.fitIntoView()
            })
        },
        /**
         * If the users generate new ERD for existing ERD worksheet
         * or a blank ERD worksheet, erdTaskKey will be re-generated
         * so the diagram must be reinitialized
         */
        watchErdTaskKey() {
            this.unwatch_erdTaskKey = this.$watch(
                'erdTaskKey',
                v => {
                    if (v) this.diagramKey = v
                },
                { immediate: true }
            )
        },
        handleDblClickNode(node) {
            const { EDIT, ALTER } = this.ENTITY_OPT_TYPES
            this.handleChooseNodeOpt({ type: this.isNewEntity(node.id) ? EDIT : ALTER, node })
        },
        isNewEntity(id) {
            return !this.initialNodes.some(n => n.id === id)
        },
        handleChooseNodeOpt({ type, node, skipZoom = false }) {
            if (this.activeErdConn.id) {
                const { ALTER, EDIT, DELETE } = this.ENTITY_OPT_TYPES
                switch (type) {
                    case ALTER:
                    case EDIT: {
                        let data = { active_entity_id: node.id }
                        if (ErdTask.getters('graphHeightPct') === 100) data.graph_height_pct = 50
                        ErdTaskTmp.update({ where: this.activeTaskId, data })
                        if (!skipZoom)
                            // call in the next tick to ensure diagramDim height is up to date
                            this.$nextTick(() => this.zoomIntoNode(node))
                        break
                    }
                    case DELETE:
                        // Remove node from staging data and diagram
                        ErdTaskTmp.update({
                            where: this.activeTaskId,
                            data(task) {
                                // close editor
                                task.active_entity_id = ''
                                task.graph_height_pct = 100
                                // remove the node and its links
                                const idx = task.data.nodes.findIndex(n => n.id === node.id)
                                task.data.nodes.splice(idx, 1)
                                task.data.links = queryHelper.getExcludedLinks({
                                    links: task.data.links,
                                    node,
                                })
                            },
                        })
                        this.$refs.diagram.removeNode(node)
                        break
                }
            } else
                this.SET_SNACK_BAR_MESSAGE({
                    text: [this.$mxs_t('errors.requiredConn')],
                    type: 'error',
                })
        },
        onNodesCoordsUpdate(v) {
            const nodeMap = this.$helpers.lodash.keyBy(v, 'id')
            // persist node coords
            ErdTask.update({
                where: this.activeTaskId,
                data: {
                    data: {
                        ...this.graphData,
                        nodes: this.graphData.nodes.map(n => {
                            if (!nodeMap[n.id]) return n
                            const { x, y, vx, vy } = nodeMap[n.id]
                            return {
                                ...n,
                                x,
                                y,
                                vx,
                                vy,
                            }
                        }),
                    },
                    is_laid_out: true,
                },
            })
            // Also update the staging data
            ErdTaskTmp.update({
                where: this.activeTaskId,
                data: {
                    data: { ...this.stagingGraphData, nodes: v },
                },
            })
        },
        fitIntoView() {
            this.setZoom({ isFitIntoView: true })
        },
        calcFitZoom({ extent: { minX, maxX, minY, maxY }, paddingPct = 2 }) {
            const graphWidth = maxX - minX
            const graphHeight = maxY - minY
            const xScale = (this.diagramDim.width / graphWidth) * (1 - paddingPct / 100)
            const yScale = (this.diagramDim.height / graphHeight) * (1 - paddingPct / 100)
            // Choose the minimum scale among xScale, yScale, and the maximum allowed scale
            let k = Math.min(xScale, yScale, this.scaleExtent[1])
            // Clamp the scale value within the scaleExtent range
            k = Math.min(Math.max(k, this.scaleExtent[0]), this.scaleExtent[1])
            return k
        },
        zoomIntoNode(node) {
            const minX = node.x - node.size.width / 2
            const minY = node.y - node.size.height / 2
            const maxX = minX + node.size.width
            const maxY = minY + node.size.height
            this.setZoom({
                isFitIntoView: true,
                customExtent: { minX, maxX, minY, maxY },
                /* add a padding of 20%, so there'd be some reserved space if the users
                 * alter the table by adding new column
                 */
                paddingPct: 20,
            })
        },
        /**
         * Get the correct dimension of the nodes for controlling the zoom
         */
        getGraphExtent() {
            return {
                minX: d3Min(this.stagingNodes, n => n.x - n.size.width / 2),
                minY: d3Min(this.stagingNodes, n => n.y - n.size.height / 2),
                maxX: d3Max(this.stagingNodes, n => n.x + n.size.width / 2),
                maxY: d3Max(this.stagingNodes, n => n.y + n.size.height / 2),
            }
        },
        /**
         * Auto adjust (zoom in or out) the contents of a graph
         * @param {Boolean} [param.isFitIntoView] - if it's true, v param will be ignored
         * @param {Object} [param.customExtent] - custom extent
         * @param {Number} [param.v] - zoom value
         */
        setZoom({ isFitIntoView = false, customExtent, v, paddingPct = 2 }) {
            this.isFitIntoView = isFitIntoView
            const extent = customExtent ? customExtent : this.getGraphExtent()
            const { minX, minY, maxX, maxY } = extent

            const k = isFitIntoView ? this.calcFitZoom({ extent, paddingPct }) : v
            const x = this.diagramDim.width / 2 - ((minX + maxX) / 2) * k
            const y = this.diagramDim.height / 2 - ((minY + maxY) / 2) * k

            this.panAndZoom = { x, y, k, transition: true }
        },
        updateNode(params) {
            this.$refs.diagram.updateNode(params)
        },
        handleCreateTable() {
            const length = this.stagingNodes.length
            const { tableParserTransformer, tableParser, genErdNode } = queryHelper
            const nodeData = tableParserTransformer({
                schema: this.$typy(ErdTask.getters('stagingSchemas'), '[0]').safeString,
                parsedTable: tableParser.parse(tableTemplate(`table_${length + 1}`)),
                charsetCollationMap: this.charset_collation_map,
            })

            const { x, y, k } = this.panAndZoom
            const node = {
                ...genErdNode({ nodeData, highlightColor: this.$helpers.dynamicColors(length) }),
                // plus extra padding
                x: (0 - x) / k + 65,
                y: (0 - y) / k + 42,
            }
            const nodes = this.$helpers.immutableUpdate(this.stagingNodes, { $push: [node] })
            ErdTaskTmp.update({
                where: this.activeTaskId,
                data: { data: { ...this.stagingGraphData, nodes } },
            }).then(() => {
                this.$refs.diagram.addNode(node)
                this.handleChooseNodeOpt({ type: this.ENTITY_OPT_TYPES.EDIT, node, skipZoom: true })
            })
        },
    },
}
</script>
<style lang="scss" scoped>
.entity-diagram {
    .setting-btn {
        visibility: hidden;
        &--visible {
            visibility: visible;
        }
    }
    .entity-table {
        &:hover {
            .setting-btn {
                visibility: visible;
            }
        }
    }
}
</style>
