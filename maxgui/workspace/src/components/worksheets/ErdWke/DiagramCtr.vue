<template>
    <div :id="diagramId" class="fill-height d-flex flex-column">
        <er-toolbar
            :graphConfig="graphConfigData"
            :height="toolbarHeight"
            :zoom="panAndZoom.k"
            :isFitIntoView="isFitIntoView"
            :exportOptions="exportOptions"
            :conn="conn"
            :nodesHistory="nodesHistory"
            :activeHistoryIdx="activeHistoryIdx"
            @set-zoom="setZoom"
            @on-create-table="handleCreateTable"
            @on-undo="navHistory(activeHistoryIdx - 1)"
            @on-redo="navHistory(activeHistoryIdx + 1)"
            @click-auto-arrange="onClickAutoArrange"
            @change-graph-config-attr-value="changeGraphConfigAttrValue"
            v-on="$listeners"
        />
        <entity-diagram
            v-if="erdTaskKey"
            ref="diagram"
            :key="erdTaskKey"
            :panAndZoom.sync="panAndZoom"
            :nodes="nodes"
            :dim="diagramDim"
            :scaleExtent="scaleExtent"
            :graphConfigData="graphConfigData"
            :isLaidOut="isLaidOut"
            :activeNodeId="activeEntityId"
            :refTargetMap="refTargetMap"
            :tablesColNameMap="tablesColNameMap"
            :colKeyCategoryMap="colKeyCategoryMap"
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
            @on-board-contextmenu="
                setCtxMenu({ type: CTX_TYPES.DIAGRAM, e: $event, item: { id: diagramId } })
            "
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
        <mxs-sub-menu
            v-if="activeCtxItemId"
            :key="activeCtxItemId"
            :value="Boolean(activeCtxItemId)"
            :items="ctxMenuItems"
            absolute
            offset-y
            :position-x="menuX"
            :position-y="menuY"
            transition="slide-y-transition"
            content-class="v-menu--mariadb v-menu--mariadb-full-border"
            :activator="activeCtxItemId ? `#${activeCtxItemId}` : ''"
            @input="activeCtxItem = null"
            @item-click="$event.action()"
        />
    </div>
</template>

<script>
/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-04-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapMutations, mapState } from 'vuex'
import ErdTask from '@wsModels/ErdTask'
import ErdTaskTmp from '@wsModels/ErdTaskTmp'
import ErToolbar from '@wsSrc/components/worksheets/ErdWke/ErToolbar.vue'
import EntityDiagram from '@wsSrc/components/worksheets/ErdWke/EntityDiagram.vue'
import { EventBus } from '@wkeComps/EventBus'
import { LINK_SHAPES } from '@share/components/common/MxsSvgGraphs/shapeConfig'
import { EVENT_TYPES } from '@share/components/common/MxsSvgGraphs/linkConfig'
import { MIN_MAX_CARDINALITY } from '@wsSrc/components/worksheets/ErdWke/config'
import tableTemplate from '@wkeComps/ErdWke/tableTemplate'
import erdHelper from '@wsSrc/utils/erdHelper'
import TableParser from '@wsSrc/utils/TableParser'
import {
    DDL_EDITOR_SPECS,
    CREATE_TBL_TOKENS,
    CTX_TYPES,
    ENTITY_OPT_TYPES,
    LINK_OPT_TYPES,
    ERD_EXPORT_OPTS,
} from '@wsSrc/constants'

export default {
    name: 'diagram-ctr',
    components: { ErToolbar, EntityDiagram },
    props: {
        isFormValid: { type: Boolean, required: true },
        dim: { type: Object, required: true },
        graphHeightPct: { type: Number, required: true },
        erdTask: { type: Object, required: true },
        conn: { type: Object, required: true },
        nodeMap: { type: Object, required: true },
        nodes: { type: Array, required: true },
        tables: { type: Array, required: true },
        schemas: { type: Array, required: true },
        activeEntityId: { type: String, required: true },
        erdTaskTmp: { type: Object, required: true },
        refTargetMap: { type: Object, required: true },
        tablesColNameMap: { type: Object, required: true },
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
            ctxMenuType: null, // CTX_TYPES
            activeCtxItem: null,
            menuX: 0,
            menuY: 0,
        }
    },
    computed: {
        ...mapState({ charset_collation_map: state => state.editorsMem.charset_collation_map }),
        connId() {
            return this.$typy(this.conn, 'id').safeString
        },
        diagramId() {
            return `${this.CTX_TYPES.DIAGRAM}_${this.erdTaskKey}`
        },
        activeGraphConfig() {
            return this.$typy(this.erdTask, 'graph_config').safeObjectOrEmpty
        },
        isLaidOut() {
            return this.$typy(this.erdTask, 'is_laid_out').safeBoolean
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
        /**
         * If the users generate new ERD for existing ERD worksheet
         * or a blank ERD worksheet, erdTaskKey will be re-generated
         * so the diagram must be reinitialized
         */
        erdTaskKey() {
            return this.$typy(this.erdTaskTmp, 'key').safeString
        },
        exportOptions() {
            return ERD_EXPORT_OPTS.map(({ text, event }) => ({
                text: this.$mxs_t(text),
                action: () => this.$emit(event),
            }))
        },
        diagramOpts() {
            return [
                {
                    text: this.$mxs_t('createTable'),
                    action: () => this.handleCreateTable(),
                },
                {
                    text: this.$mxs_t('fitDiagramInView'),
                    action: () => this.fitIntoView(),
                },
                {
                    text: this.$mxs_t('autoArrangeErd'),
                    action: () => this.onClickAutoArrange(),
                },
                {
                    text: this.$mxs_t(
                        this.graphConfigData.link.isAttrToAttr
                            ? 'disableDrawingFksToCols'
                            : 'enableDrawingFksToCols'
                    ),
                    action: () =>
                        this.changeGraphConfigAttrValue({
                            path: 'link.isAttrToAttr',
                            value: !this.graphConfigData.link.isAttrToAttr,
                        }),
                },
                {
                    text: this.$mxs_t(
                        this.graphConfigData.link.isHighlightAll
                            ? 'turnOffRelationshipHighlight'
                            : 'turnOnRelationshipHighlight'
                    ),
                    action: () =>
                        this.changeGraphConfigAttrValue({
                            path: 'link.isHighlightAll',
                            value: !this.graphConfigData.link.isHighlightAll,
                        }),
                },
                {
                    text: this.$mxs_t('export'),
                    children: this.exportOptions,
                },
            ]
        },
        entityOpts() {
            return Object.values(ENTITY_OPT_TYPES).map(type => ({
                type,
                text: this.$mxs_t(type),
                action: () => this.handleChooseNodeOpt({ type, node: this.activeCtxItem }),
            }))
        },
        linkOpts() {
            const { EDIT, REMOVE } = LINK_OPT_TYPES
            const link = this.activeCtxItem
            let opts = [
                { text: this.$mxs_t(EDIT), type: EDIT },
                { text: this.$mxs_t(REMOVE), type: REMOVE },
            ]
            if (link) {
                opts.push(this.genCardinalityOpt(link))
                const { primaryKey } = CREATE_TBL_TOKENS
                const {
                    relationshipData: { src_attr_id, target_attr_id },
                } = link
                const colKeyCategories = this.colKeyCategoryMap[src_attr_id]
                const refColKeyCategories = this.colKeyCategoryMap[target_attr_id]
                if (!colKeyCategories.includes(primaryKey))
                    opts.push(this.genOptionalityOpt({ link }))
                if (!refColKeyCategories.includes(primaryKey))
                    opts.push(this.genOptionalityOpt({ link, isForRefTbl: true }))
            }
            return opts.map(opt => ({ ...opt, action: () => this.handleChooseLinkOpt(opt.type) }))
        },
        ctxMenuItems() {
            const { NODE, LINK, DIAGRAM } = this.CTX_TYPES
            switch (this.ctxMenuType) {
                case DIAGRAM:
                    return this.diagramOpts
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
            return this.$typy(this.erdTaskTmp, 'nodes_history').safeArray
        },
        activeHistoryIdx() {
            return this.$typy(this.erdTaskTmp, 'active_history_idx').safeNumber
        },
        colKeyCategoryMap() {
            return this.tables.reduce((map, tbl) => {
                map = { ...map, ...erdHelper.genColKeyTypeMap(tbl.defs.key_category_map) }
                return map
            }, {})
        },
    },
    watch: {
        graphConfigData: {
            deep: true,
            handler(v) {
                ErdTask.update({
                    where: this.erdTask.id,
                    data: {
                        graph_config: this.$helpers.immutableUpdate(this.activeGraphConfig, {
                            link: {
                                isAttrToAttr: { $set: v.link.isAttrToAttr },
                                isHighlightAll: { $set: v.link.isHighlightAll },
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
        activeEntityId(v) {
            if (!v) this.fitIntoView()
        },
    },
    created() {
        this.CTX_TYPES = CTX_TYPES
        this.graphConfigData = this.$helpers.lodash.merge(
            this.graphConfigData,
            this.activeGraphConfig
        )
    },
    activated() {
        this.eventBus.$on('entity-editor-ctr-update-node-data', this.updateNode)
    },
    deactivated() {
        this.eventBus.$off('entity-editor-ctr-update-node-data')
    },
    beforeDestroy() {
        this.eventBus.$off('entity-editor-ctr-update-node-data')
    },
    methods: {
        ...mapMutations({ SET_SNACK_BAR_MESSAGE: 'mxsApp/SET_SNACK_BAR_MESSAGE' }),
        onRendered(diagram) {
            this.onNodesCoordsUpdate(diagram.nodes)
            if (diagram.nodes.length) this.fitIntoView()
        },
        handleDblClickNode(node) {
            this.handleChooseNodeOpt({ type: ENTITY_OPT_TYPES.EDIT, node })
        },
        genCardinalityOpt(link) {
            const { SET_ONE_TO_ONE, SET_ONE_TO_MANY } = LINK_OPT_TYPES
            const { ONLY_ONE, ZERO_OR_ONE } = MIN_MAX_CARDINALITY
            const [src = ''] = link.relationshipData.type.split(':')
            const optType =
                src === ONLY_ONE || src === ZERO_OR_ONE ? SET_ONE_TO_MANY : SET_ONE_TO_ONE
            return { text: this.$mxs_t(optType), type: optType }
        },
        genOptionalityOpt({ link, isForRefTbl = false }) {
            const {
                SET_MANDATORY,
                SET_FK_COL_OPTIONAL,
                SET_REF_COL_MANDATORY,
                SET_REF_COL_OPTIONAL,
            } = LINK_OPT_TYPES
            const {
                source,
                target,
                relationshipData: { src_attr_id, target_attr_id },
            } = link
            let node = source,
                colId = src_attr_id,
                optType = isForRefTbl ? SET_REF_COL_MANDATORY : SET_MANDATORY

            if (isForRefTbl) {
                node = target
                colId = target_attr_id
            }
            if (erdHelper.isColMandatory({ node, colId }))
                optType = isForRefTbl ? SET_REF_COL_OPTIONAL : SET_FK_COL_OPTIONAL

            return { text: this.$mxs_t(optType), type: optType }
        },
        setCtxMenu({ e, type, item }) {
            this.menuX = e.clientX
            this.menuY = e.clientY
            this.ctxMenuType = type
            this.activeCtxItem = item
        },
        handleChooseNodeOpt({ type, node, skipZoom = false }) {
            const { EDIT, REMOVE } = ENTITY_OPT_TYPES
            switch (type) {
                case EDIT: {
                    this.handleOpenEditor({ node, spec: DDL_EDITOR_SPECS.COLUMNS })
                    if (this.connId && !skipZoom)
                        // call in the next tick to ensure diagramDim height is up to date
                        this.$nextTick(() => this.zoomIntoNode(node))

                    break
                }
                case REMOVE: {
                    const { foreignKey } = CREATE_TBL_TOKENS
                    const nodeMap = this.nodes.reduce((map, n) => {
                        if (n.id !== node.id) {
                            const fkMap = n.data.defs.key_category_map[foreignKey]
                            if (!fkMap) map[n.id] = n
                            else {
                                const updatedFkMap = Object.values(fkMap).reduce((res, key) => {
                                    if (key.ref_tbl_id !== node.id) res[key.id] = key
                                    return res
                                }, {})
                                map[n.id] = this.$helpers.immutableUpdate(n, {
                                    data: {
                                        defs: {
                                            key_category_map: Object.keys(updatedFkMap).length
                                                ? { $merge: { [foreignKey]: updatedFkMap } }
                                                : { $unset: [foreignKey] },
                                        },
                                    },
                                })
                            }
                        }

                        return map
                    }, {})
                    this.closeEditor()
                    this.updateAndDrawNodes({ nodeMap })
                    break
                }
            }
        },
        handleChooseLinkOpt(type) {
            const link = this.activeCtxItem
            const {
                EDIT,
                REMOVE,
                SET_ONE_TO_ONE,
                SET_ONE_TO_MANY,
                SET_MANDATORY,
                SET_FK_COL_OPTIONAL,
                SET_REF_COL_MANDATORY,
                SET_REF_COL_OPTIONAL,
            } = LINK_OPT_TYPES
            switch (type) {
                case EDIT:
                    this.handleOpenEditor({ node: link.source, spec: DDL_EDITOR_SPECS.FK })
                    if (this.connId) this.$nextTick(() => this.zoomIntoNode(link.source))
                    break
                case REMOVE: {
                    const { foreignKey } = CREATE_TBL_TOKENS
                    let fkMap = this.$typy(
                        this.nodeMap[link.source.id],
                        `data.defs.key_category_map[${foreignKey}]`
                    ).safeObjectOrEmpty
                    fkMap = this.$helpers.immutableUpdate(fkMap, { $unset: [link.id] })
                    const nodeMap = this.$helpers.immutableUpdate(this.nodeMap, {
                        [link.source.id]: {
                            data: {
                                defs: {
                                    key_category_map: Object.keys(fkMap).length
                                        ? { $merge: { [foreignKey]: fkMap } }
                                        : { $unset: [foreignKey] },
                                },
                            },
                        },
                    })
                    this.updateAndDrawNodes({ nodeMap })
                    break
                }
                case SET_ONE_TO_MANY:
                case SET_ONE_TO_ONE:
                case SET_FK_COL_OPTIONAL:
                case SET_MANDATORY:
                case SET_REF_COL_OPTIONAL:
                case SET_REF_COL_MANDATORY:
                    this.updateCardinality({ type, link })
                    break
            }
        },
        updateCardinality({ type, link }) {
            const {
                SET_ONE_TO_ONE,
                SET_ONE_TO_MANY,
                SET_MANDATORY,
                SET_FK_COL_OPTIONAL,
                SET_REF_COL_MANDATORY,
                SET_REF_COL_OPTIONAL,
            } = LINK_OPT_TYPES
            let nodeMap = this.nodeMap
            const { src_attr_id, target_attr_id } = link.relationshipData
            let method,
                nodeId = link.source.id,
                node = this.nodeMap[nodeId],
                colId = src_attr_id,
                value = false
            switch (type) {
                case SET_ONE_TO_MANY:
                case SET_ONE_TO_ONE: {
                    method = 'toggleUnique'
                    /**
                     * In an one to many relationship, FK is placed on the "many" side,
                     * and the FK col can't be unique. On the other hand, one to one
                     * relationship, fk col and ref col must be both unique
                     */
                    if (type === SET_ONE_TO_ONE) {
                        value = true
                        // update also ref col of target node
                        nodeMap = this.$helpers.immutableUpdate(nodeMap, {
                            [link.target.id]: {
                                $set: this[method]({
                                    node: this.nodeMap[link.target.id],
                                    colId: target_attr_id,
                                    value,
                                }),
                            },
                        })
                    }
                    break
                }
                case SET_MANDATORY:
                case SET_FK_COL_OPTIONAL:
                case SET_REF_COL_OPTIONAL:
                case SET_REF_COL_MANDATORY: {
                    method = 'toggleNotNull'
                    if (type === SET_REF_COL_OPTIONAL || type === SET_REF_COL_MANDATORY) {
                        nodeId = link.target.id
                        node = this.nodeMap[nodeId]
                        colId = target_attr_id
                    }
                    value = type === SET_MANDATORY || type === SET_REF_COL_MANDATORY

                    break
                }
            }
            nodeMap = this.$helpers.immutableUpdate(nodeMap, {
                [nodeId]: { $set: this[method]({ node, colId, value }) },
            })
            this.updateAndDrawNodes({ nodeMap })
        },
        /**
         * @param {object} param
         * @param {object} param.node - entity-diagram node
         * @param {string} param.colId - column id
         * @param {boolean} param.value - if it's true, add UQ key if not exists, otherwise remove UQ
         * @return {object} updated node
         */
        toggleUnique({ node, colId, value }) {
            const category = CREATE_TBL_TOKENS.uniqueKey
            // check if column is already unique
            const isUnique = erdHelper.areUniqueCols({ node, colIds: [colId] })
            if (value && isUnique) return node
            let keyMap = node.data.defs.key_category_map[category] || {}
            // add UQ key
            if (value) {
                const newKey = erdHelper.genKey({
                    defs: node.data.defs,
                    category,
                    colId,
                })
                keyMap = this.$helpers.immutableUpdate(keyMap, { $merge: { [newKey.id]: newKey } })
            }
            // remove UQ key
            else
                keyMap = this.$helpers.immutableUpdate(keyMap, {
                    $unset: Object.values(keyMap).reduce((ids, k) => {
                        if (
                            this.$helpers.lodash.isEqual(
                                k.cols.map(c => c.id),
                                [colId]
                            )
                        )
                            ids.push(k.id)
                        return ids
                    }, []),
                })

            return this.$helpers.immutableUpdate(node, {
                data: {
                    defs: {
                        key_category_map: Object.keys(keyMap).length
                            ? { $merge: { [category]: keyMap } }
                            : { $unset: [category] },
                    },
                },
            })
        },
        /**
         * @param {object} param
         * @param {object} param.node - entity-diagram node
         * @param {string} param.colId - column id
         * @param {boolean} param.value - if it's true, turns on NOT NULL.
         * @return {object} updated node
         */
        toggleNotNull({ node, colId, value }) {
            return this.$helpers.immutableUpdate(node, {
                data: { defs: { col_map: { [colId]: { nn: { $set: value } } } } },
            })
        },
        assignCoord(nodeMap) {
            return this.nodes.reduce((map, n) => {
                if (!nodeMap[n.id]) map[n.id] = n
                else {
                    const { x, y, vx, vy, size } = nodeMap[n.id]
                    map[n.id] = {
                        ...n,
                        x,
                        y,
                        vx,
                        vy,
                        size,
                    }
                }
                return map
            }, {})
        },
        /**
         * @param {array} v - diagram staging nodes with new coordinate values
         */
        onNodesCoordsUpdate(v) {
            const nodeMap = this.assignCoord(this.$helpers.lodash.keyBy(v, 'id'))
            ErdTask.update({
                where: this.erdTask.id,
                data: { nodeMap, is_laid_out: true },
            })
            ErdTask.dispatch('updateNodesHistory', nodeMap)
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
            if (this.connId) {
                const length = this.nodes.length
                const { genDdlEditorData, genErdNode } = erdHelper
                const { dynamicColors, immutableUpdate } = this.$helpers
                const schema = this.$typy(this.schemas, '[0]').safeString || 'test'
                const tableParser = new TableParser()
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
                const nodeMap = immutableUpdate(this.nodeMap, { $merge: { [node.id]: node } })
                ErdTask.update({
                    where: this.erdTask.id,
                    data: { nodeMap },
                }).then(() => {
                    ErdTask.dispatch('updateNodesHistory', nodeMap)
                    this.$refs.diagram.addNode(node)
                    this.handleChooseNodeOpt({
                        type: ENTITY_OPT_TYPES.EDIT,
                        node,
                        skipZoom: true,
                    })
                })
            } else
                this.SET_SNACK_BAR_MESSAGE({
                    text: [this.$mxs_t('errors.requiredConn')],
                    type: 'error',
                })
        },
        handleOpenEditor({ node, spec }) {
            if (this.connId) {
                let data = { active_entity_id: node.id, active_spec: spec }
                if (this.graphHeightPct === 100) data.graph_height_pct = 40
                ErdTaskTmp.update({ where: this.erdTask.id, data })
            } else
                this.SET_SNACK_BAR_MESSAGE({
                    text: [this.$mxs_t('errors.requiredConn')],
                    type: 'error',
                })
        },
        closeEditor() {
            ErdTaskTmp.update({
                where: this.erdTask.id,
                data: { active_entity_id: '', graph_height_pct: 100 },
            })
        },
        updateAndDrawNodes({ nodeMap, skipHistory }) {
            ErdTask.update({ where: this.erdTask.id, data: { nodeMap } }).then(() => {
                this.$refs.diagram.update(this.nodes)
                if (!skipHistory) ErdTask.dispatch('updateNodesHistory', nodeMap)
            })
        },
        redrawnDiagram() {
            const nodeMap = this.nodesHistory[this.activeHistoryIdx]
            this.updateAndDrawNodes({ nodeMap, skipHistory: true })
        },
        navHistory(idx) {
            ErdTask.dispatch('updateActiveHistoryIdx', idx).then(() => this.redrawnDiagram())
        },
        /**
         * Adds a PLAIN index for provided colId to provided node
         * @param {object} param
         * @param {string} param.colId
         * @param {object} param.node
         * @returns {object} updated node
         */
        addPlainIndex({ colId, node }) {
            const { key } = CREATE_TBL_TOKENS
            const refTblDef = node.data.defs
            const plainKeyMap = this.$typy(refTblDef, `key_category_map[${key}]`).safeObjectOrEmpty
            const newKey = erdHelper.genKey({ defs: refTblDef, category: key, colId })
            return this.$helpers.immutableUpdate(node, {
                data: {
                    defs: {
                        key_category_map: {
                            $merge: { [key]: { ...plainKeyMap, [newKey.id]: newKey } },
                        },
                    },
                },
            })
        },
        onCreateNewFk({ node, currentFkMap, newKey, refNode }) {
            const { foreignKey } = CREATE_TBL_TOKENS
            const { immutableUpdate } = this.$helpers
            let nodeMap = this.nodeMap

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
                const nonIndexedColId = this.colKeyCategoryMap[refColId] ? null : refColId
                if (nonIndexedColId) {
                    nodeMap = immutableUpdate(nodeMap, {
                        [refNode.id]: {
                            $set: this.addPlainIndex({
                                node: nodeMap[refNode.id],
                                colId: nonIndexedColId,
                            }),
                        },
                    })
                }

                // Add FK
                nodeMap = immutableUpdate(nodeMap, {
                    [node.id]: {
                        data: {
                            defs: {
                                key_category_map: {
                                    $merge: {
                                        [foreignKey]: { ...currentFkMap, [newKey.id]: newKey },
                                    },
                                },
                            },
                        },
                    },
                })
                this.updateAndDrawNodes({ nodeMap })
            } else {
                this.SET_SNACK_BAR_MESSAGE({
                    text: [this.$mxs_t('errors.fkColsRequirements')],
                    type: 'error',
                })
            }
        },
        onClickAutoArrange() {
            ErdTask.update({
                where: this.erdTask.id,
                data: { is_laid_out: false },
            }).then(() => this.$refs.diagram.runSimulation(diagram => this.onRendered(diagram)))
        },
        immutableUpdateConfig(obj, path, value) {
            const updatedObj = this.$helpers.lodash.cloneDeep(obj)
            this.$helpers.lodash.update(updatedObj, path, () => value)
            return updatedObj
        },
        changeGraphConfigAttrValue({ path, value }) {
            this.graphConfigData = this.immutableUpdateConfig(this.graphConfigData, path, value)
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
