<template>
    <div class="fill-height d-flex flex-column">
        <er-toolbar-ctr
            v-model="graphConfigData"
            :height="toolbarHeight"
            :zoom="panAndZoom.k"
            :isFitIntoView="isFitIntoView"
            @set-zoom="setZoom"
            @on-create-table="handleCreateTable"
            @on-undo="navHistory(activeHistoryIdx - 1)"
            @on-redo="navHistory(activeHistoryIdx + 1)"
            v-on="$listeners"
        />
        <entity-diagram
            v-if="diagramKey"
            ref="diagram"
            :key="diagramKey"
            :panAndZoom.sync="panAndZoom"
            :nodes="nodes"
            :dim="diagramDim"
            :scaleExtent="scaleExtent"
            :graphConfigData="graphConfigData"
            :isLaidOut="isLaidOut"
            :activeNodeId="activeEntityId"
            :refTargetMap="refTargetMap"
            :tablesColNameMap="tablesColNameMap"
            :colKeyTypeMap="colKeyTypeMap"
            class="entity-diagram"
            @on-rendered.once="onRendered"
            @on-node-drag-end="onNodeDragEnd"
            @dblclick="isFormValid ? handleDblClickNode($event) : null"
            @on-create-new-fk="onCreateNewFk"
            @on-node-contextmenu="
                setCtxMenu({ type: CTX_TYPES.NODE, e: $event.e, item: $event.node })
            "
            @on-link-contextmenu="
                setCtxMenu({ type: CTX_TYPES.LINK, e: $event.e, item: $event.link })
            "
            @on-board-contextmenu="e => e.preventDefault()"
        >
            <template v-slot:entity-setting-btn="{ node }">
                <v-btn
                    :id="node.id"
                    x-small
                    class="setting-btn"
                    :class="{
                        'setting-btn--visible': activeCtxItemId === node.id,
                    }"
                    icon
                    color="primary"
                    :disabled="!isFormValid"
                    @click.stop="setCtxMenu({ e: $event, type: CTX_TYPES.NODE, item: node })"
                >
                    <v-icon size="14">
                        $vuetify.icons.mxs_settings
                    </v-icon>
                </v-btn>
            </template>
        </entity-diagram>
        <v-menu
            :key="activeCtxItemId"
            :value="Boolean(activeCtxItemId)"
            absolute
            offset-y
            :position-x="menuX"
            :position-y="menuY"
            transition="slide-y-transition"
            content-class="v-menu--mariadb v-menu--mariadb-full-border"
            @input="activeCtxItem = null"
        >
            <v-list>
                <v-list-item
                    v-for="(opt, i) in ctxMenuItems"
                    :key="i"
                    dense
                    link
                    class="px-2"
                    @click="ctxMenuHandler(opt.type)"
                >
                    <v-list-item-title class="mxs-color-helper text-text">
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
import ErToolbarCtr from '@wkeComps/ErdWke/ErToolbarCtr.vue'
import EntityDiagram from '@wsSrc/components/worksheets/ErdWke/EntityDiagram.vue'
import { EventBus } from '@wkeComps/EventBus'
import { LINK_SHAPES } from '@share/components/common/MxsSvgGraphs/shapeConfig'
import { EVENT_TYPES } from '@share/components/common/MxsSvgGraphs/linkConfig'
import tableTemplate from '@wkeComps/ErdWke/tableTemplate'
import erdHelper from '@wsSrc/utils/erdHelper'

export default {
    name: 'diagram-ctr',
    components: { ErToolbarCtr, EntityDiagram },
    props: {
        dim: { type: Object, required: true },
        connId: { type: String, required: true },
        isFormValid: { type: Boolean, required: true },
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
            ctxMenuType: null, // CTX_TYPES
            activeCtxItem: null,
            menuX: 0,
            menuY: 0,
        }
    },
    computed: {
        ...mapState({
            charset_collation_map: state => state.editorsMem.charset_collation_map,
            CTX_TYPES: state => state.mxsWorkspace.config.CTX_TYPES,
            ENTITY_OPT_TYPES: state => state.mxsWorkspace.config.ENTITY_OPT_TYPES,
            LINK_OPT_TYPES: state => state.mxsWorkspace.config.LINK_OPT_TYPES,
            CREATE_TBL_TOKENS: state => state.mxsWorkspace.config.CREATE_TBL_TOKENS,
            DDL_EDITOR_SPECS: state => state.mxsWorkspace.config.DDL_EDITOR_SPECS,
        }),
        activeRecord() {
            return ErdTask.getters('activeRecord')
        },
        activeTaskId() {
            return ErdTask.getters('activeRecordId')
        },
        nodes() {
            return ErdTask.getters('nodes')
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
            const { EDIT, REMOVE } = this.ENTITY_OPT_TYPES
            return [
                { text: this.$mxs_t('editTbl'), type: EDIT },
                { text: this.$mxs_t('removeFromDiagram'), type: REMOVE },
            ]
        },
        linkOpts() {
            const { EDIT, REMOVE, CHANGE_RELATIONSHIP } = this.LINK_OPT_TYPES
            return [
                { text: this.$mxs_t('editFk'), type: EDIT },
                { text: this.$mxs_t('removeFk'), type: REMOVE },
                { text: this.$mxs_t('changeRelationshipType'), type: CHANGE_RELATIONSHIP },
            ]
        },
        ctxMenuItems() {
            const { NODE, LINK } = this.CTX_TYPES
            switch (this.ctxMenuType) {
                case NODE:
                    return this.entityOpts
                case LINK:
                    return this.linkOpts
                default:
                    return []
            }
        },
        activeCtxItemId() {
            return this.$typy(this.activeCtxItem, 'id').safeString
        },
        eventBus() {
            return EventBus
        },
        nodesHistory() {
            return ErdTask.getters('nodesHistory')
        },
        activeHistoryIdx() {
            return ErdTask.getters('activeHistoryIdx')
        },
        refTargetMap() {
            return ErdTask.getters('refTargetMap')
        },
        tablesColNameMap() {
            return ErdTask.getters('tablesColNameMap')
        },
        colKeyTypeMap() {
            return ErdTask.getters('colKeyTypeMap')
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
        this.eventBus.$off('entity-editor-ctr-update-node-data')
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
        onRendered(diagram) {
            this.onNodesCoordsUpdate(diagram.nodes)
            if (diagram.nodes.length) this.fitIntoView()
        },
        handleDblClickNode(node) {
            this.handleChooseNodeOpt({ type: this.ENTITY_OPT_TYPES.EDIT, node })
        },
        setCtxMenu({ e, type, item }) {
            this.menuX = e.clientX
            this.menuY = e.clientY
            this.ctxMenuType = type
            this.activeCtxItem = item
        },
        ctxMenuHandler(type) {
            const { NODE, LINK } = this.CTX_TYPES
            switch (this.ctxMenuType) {
                case NODE:
                    this.handleChooseNodeOpt({ type, node: this.activeCtxItem })
                    break
                case LINK:
                    this.handleChooseLinkOpt({ type, link: this.activeCtxItem })
                    break
            }
        },
        handleChooseNodeOpt({ type, node, skipZoom = false }) {
            if (this.connId) {
                const { EDIT, REMOVE } = this.ENTITY_OPT_TYPES
                switch (type) {
                    case EDIT: {
                        this.openEditor({ node, spec: this.DDL_EDITOR_SPECS.COLUMNS })
                        if (!skipZoom)
                            // call in the next tick to ensure diagramDim height is up to date
                            this.$nextTick(() => this.zoomIntoNode(node))
                        break
                    }
                    case REMOVE: {
                        const { foreignKey } = this.CREATE_TBL_TOKENS
                        let nodes = this.nodes
                        nodes = nodes.filter(n => n.id !== node.id)
                        nodes = nodes.map(n => {
                            const fks = this.$typy(n, `data.definitions.keys[${foreignKey}]`)
                                .safeArray
                            if (!fks.length) return n
                            const remainingFks = fks.filter(key => key.ref_tbl_id !== node.id)
                            return this.$helpers.immutableUpdate(n, {
                                data: {
                                    definitions: {
                                        keys: remainingFks.length
                                            ? { $merge: { [foreignKey]: remainingFks } }
                                            : { $unset: [foreignKey] },
                                    },
                                },
                            })
                        })
                        this.closeEditor()
                        this.updateAndDrawNodes({ nodes })
                        break
                    }
                }
            } else
                this.SET_SNACK_BAR_MESSAGE({
                    text: [this.$mxs_t('errors.requiredConn')],
                    type: 'error',
                })
        },
        handleChooseLinkOpt({ type, link }) {
            const { EDIT, REMOVE, CHANGE_RELATIONSHIP } = this.LINK_OPT_TYPES
            switch (type) {
                case EDIT: {
                    if (this.connId) {
                        this.openEditor({ node: link.source, spec: this.DDL_EDITOR_SPECS.FK })
                        this.$nextTick(() => this.zoomIntoNode(link.source))
                    } else
                        this.SET_SNACK_BAR_MESSAGE({
                            text: [this.$mxs_t('errors.requiredConn')],
                            type: 'error',
                        })
                    break
                }
                case REMOVE: {
                    const { foreignKey } = this.CREATE_TBL_TOKENS
                    let nodeIdx = this.nodes.findIndex(n => n.id === link.source.id)
                    let fks = this.$typy(
                        this.nodes[nodeIdx],
                        `data.definitions.keys[${foreignKey}]`
                    ).safeArray
                    fks = fks.filter(k => k.id !== link.id)
                    const nodes = this.$helpers.immutableUpdate(this.nodes, {
                        [nodeIdx]: {
                            data: {
                                definitions: {
                                    keys: fks.length
                                        ? { $merge: { [foreignKey]: fks } }
                                        : { $unset: [foreignKey] },
                                },
                            },
                        },
                    })
                    this.updateAndDrawNodes({ nodes })
                    break
                }
                case CHANGE_RELATIONSHIP:
                    //TODO: Open select-dlg to choose a relationship type
                    break
            }
        },
        assignCoord({ nodeMap, nodes }) {
            return nodes.map(n => {
                if (!nodeMap[n.id]) return n
                const { x, y, vx, vy, size } = nodeMap[n.id]
                let res = {
                    ...n,
                    x,
                    y,
                    vx,
                    vy,
                    size,
                }
                return res
            })
        },
        /**
         * @param {array} v - diagram staging nodes with new coordinate values
         */
        onNodesCoordsUpdate(v) {
            const nodeMap = this.$helpers.lodash.keyBy(v, 'id')
            const nodes = this.assignCoord({ nodeMap, nodes: this.nodes })
            ErdTask.update({
                where: this.activeTaskId,
                data: { nodes, is_laid_out: true },
            })
            ErdTask.dispatch('updateNodesHistory', nodes)
        },
        /**
         * @param {object} node - node with new coordinates
         */
        onNodeDragEnd(node) {
            this.onNodesCoordsUpdate([node])
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
         * Auto adjust (zoom in or out) the contents of a graph
         * @param {Boolean} [param.isFitIntoView] - if it's true, v param will be ignored
         * @param {Object} [param.customExtent] - custom extent
         * @param {Number} [param.v] - zoom value
         */
        setZoom({ isFitIntoView = false, customExtent, v, paddingPct = 2 }) {
            this.isFitIntoView = isFitIntoView
            const extent = customExtent ? customExtent : this.$refs.diagram.getGraphExtent()
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
            const length = this.nodes.length
            const { genDdlEditorData, genErdNode } = erdHelper
            const { tableParser, dynamicColors, immutableUpdate } = this.$helpers
            const schema = this.$typy(ErdTask.getters('schemas'), '[0]').safeString || 'test'
            const nodeData = genDdlEditorData({
                parsedTable: tableParser.parse({
                    ddl: tableTemplate(`table_${length + 1}`),
                    schema,
                    autoGenId: true,
                }),
                charsetCollationMap: this.charset_collation_map,
            })

            const { x, y, k } = this.panAndZoom
            const node = {
                ...genErdNode({ nodeData, highlightColor: dynamicColors(length) }),
                // plus extra padding
                x: (0 - x) / k + 65,
                y: (0 - y) / k + 42,
            }
            const nodes = immutableUpdate(this.nodes, { $push: [node] })
            ErdTask.update({
                where: this.activeTaskId,
                data: { nodes },
            }).then(() => {
                ErdTask.dispatch('updateNodesHistory', this.nodes)
                this.$refs.diagram.addNode(node)
                this.handleChooseNodeOpt({ type: this.ENTITY_OPT_TYPES.EDIT, node, skipZoom: true })
            })
        },
        openEditor({ node, spec }) {
            let data = { active_entity_id: node.id, active_spec: spec }
            if (ErdTask.getters('graphHeightPct') === 100) data.graph_height_pct = 40
            ErdTaskTmp.update({ where: this.activeTaskId, data })
        },
        closeEditor() {
            ErdTaskTmp.update({
                where: this.activeTaskId,
                data: { active_entity_id: '', graph_height_pct: 100 },
            })
        },
        updateAndDrawNodes({ nodes, skipHistory }) {
            ErdTask.update({ where: this.activeTaskId, data: { nodes } })
            this.$refs.diagram.update(nodes)
            if (!skipHistory) ErdTask.dispatch('updateNodesHistory', nodes)
        },
        redrawnDiagram() {
            const nodes = this.nodesHistory[this.activeHistoryIdx]
            this.closeEditor()
            this.updateAndDrawNodes({ nodes, skipHistory: true })
        },
        navHistory(idx) {
            ErdTask.dispatch('updateActiveHistoryIdx', idx)
            this.redrawnDiagram()
        },
        /**
         * Adds a PLAIN index for provided colId to provided node
         * @param {object} param
         * @param {string} param.colId
         * @param {object} param.node
         * @returns {object} updated node
         */
        addPlainIndex({ colId, node }) {
            const { key } = this.CREATE_TBL_TOKENS
            const refTblDef = node.data.definitions
            const plainKeys = this.$typy(refTblDef, `keys[${key}]`).safeArray
            const newKey = erdHelper.genKey({ definitions: refTblDef, category: key, colId })
            return this.$helpers.immutableUpdate(node, {
                data: {
                    definitions: {
                        keys: { $merge: { [key]: [...plainKeys, newKey] } },
                    },
                },
            })
        },
        onCreateNewFk({ node, currentFks, newKey, refNode }) {
            const { foreignKey } = this.CREATE_TBL_TOKENS
            const { immutableUpdate } = this.$helpers
            let nodes = this.nodes

            // entity-diagram doesn't generate composite FK,so both cols and ref_cols always have one item
            const colId = newKey.cols[0].id
            const refColId = newKey.ref_cols[0].id
            // Compare column types
            if (
                erdHelper.validateFkColTypes({
                    src: node,
                    target: refNode,
                    colId,
                    targetColId: refColId,
                })
            ) {
                // Auto adds a PLAIN index for referenced col if there is none.
                const nonIndexedColId = this.colKeyTypeMap[refColId] ? null : refColId
                if (nonIndexedColId) {
                    nodes = immutableUpdate(nodes, {
                        [refNode.index]: {
                            $set: this.addPlainIndex({
                                node: nodes[refNode.index],
                                colId: nonIndexedColId,
                            }),
                        },
                    })
                }

                // Add FK
                nodes = immutableUpdate(nodes, {
                    [node.index]: {
                        data: {
                            definitions: {
                                keys: { $merge: { [foreignKey]: [...currentFks, newKey] } },
                            },
                        },
                    },
                })
                this.updateAndDrawNodes({ nodes })
            } else {
                this.SET_SNACK_BAR_MESSAGE({
                    text: [this.$mxs_t('errors.fkColsRequirements')],
                    type: 'error',
                })
            }
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
