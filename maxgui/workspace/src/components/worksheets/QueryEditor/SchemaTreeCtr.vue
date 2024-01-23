<template>
    <!-- TODO: Virtual scroll treeview -->
    <div v-if="dbTreeData.length">
        <mxs-treeview
            class="mxs-treeview"
            :items="dbTreeData"
            :search="filterTxt"
            :filter="filter"
            hoverable
            dense
            open-on-click
            transition
            :load-children="handleLoadChildren"
            :active.sync="activeNodes"
            :open.sync="expandedNodes"
            return-object
            @item:contextmenu="onContextMenu"
            @item:hovered="hoveredNode = $event"
            @item:dblclick="onNodeDblClick"
        >
            <template v-slot:label="{ item: node }">
                <div
                    :id="`node-tooltip-activator-${node.key}`"
                    class="d-flex align-center"
                    :class="{ 'cursor--grab': node.draggable }"
                    @mousedown="node.draggable ? onNodeDragStart($event) : null"
                >
                    <mxs-schema-node-icon class="mr-1" :node="node" :size="12" />
                    <span
                        v-mxs-highlighter="{ keyword: filterTxt, txt: node.name }"
                        class="text-truncate d-inline-block node-name"
                        :class="{
                            'font-weight-bold':
                                node.type === NODE_TYPES.SCHEMA &&
                                activeQueryTabConn.active_db === node.qualified_name,
                        }"
                    >
                        {{ node.name }}
                    </span>
                    <span class="text-truncate d-inline-block grayed-out-info ml-1">
                        <template v-if="$typy(node, 'data.COLUMN_TYPE').safeString">
                            {{ $typy(node, 'data.COLUMN_TYPE').safeString }}
                        </template>
                        <template
                            v-if="
                                node.type === NODE_TYPES.IDX &&
                                    $typy(node, 'data.COLUMN_NAME').safeString
                            "
                        >
                            {{ $typy(node, 'data.COLUMN_NAME').safeString }}
                        </template>
                    </span>
                </div>
            </template>
            <template v-slot:append="{ isHover, item: node }">
                <v-btn
                    v-if="node.type === NODE_TYPES.TBL || node.type === NODE_TYPES.VIEW"
                    v-show="isHover"
                    :id="`prvw-btn-tooltip-activator-${node.key}`"
                    icon
                    x-small
                    :disabled="!isSqlEditor"
                    class="mr-1"
                    @click.stop="previewNode(node)"
                >
                    <v-icon size="14" color="primary">mdi-table-eye</v-icon>
                </v-btn>
                <v-btn
                    v-show="nodesHaveCtxMenu.includes(node.type) && (isHover || showCtxBtn(node))"
                    :id="`ctx-menu-activator-${node.key}`"
                    icon
                    x-small
                    @click="e => handleOpenCtxMenu({ e, node })"
                >
                    <v-icon size="14" color="primary">mdi-dots-horizontal</v-icon>
                </v-btn>
            </template>
        </mxs-treeview>
        <v-tooltip
            v-if="hoveredNode"
            top
            class="preview-data-tooltip"
            :activator="`#prvw-btn-tooltip-activator-${hoveredNode.key}`"
        >
            {{ $mxs_t('previewData') }}
        </v-tooltip>
        <v-tooltip
            v-if="hoveredNode && nodesHaveCtxMenu.includes(hoveredNode.type)"
            class="node-tooltip"
            :value="Boolean(hoveredNode)"
            :disabled="isDragging"
            right
            :nudge-right="45"
            transition="slide-x-transition"
            :activator="`#node-tooltip-activator-${hoveredNode.key}`"
        >
            <table class="node-tooltip">
                <tbody>
                    <tr v-for="(value, key) in hoveredNode.data" :key="key">
                        <td class="font-weight-bold pr-2">{{ key }}:</td>
                        <td>{{ value }}</td>
                    </tr>
                </tbody>
            </table>
        </v-tooltip>
        <mxs-sub-menu
            v-if="activeCtxNode"
            :key="activeCtxNode.key"
            v-model="showCtxMenu"
            :left="true"
            :offset-y="true"
            transition="slide-y-transition"
            :nudge-right="12"
            :nudge-bottom="10"
            :items="activeCtxItemOpts"
            :activator="`#ctx-menu-activator-${activeCtxNode.key}`"
            @item-click="optionHandler({ node: activeCtxNode, opt: $event })"
        />
    </div>
</template>
<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
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
This component emits the following events
@get-node-data: { query_mode: string, qualified_name:string }
@place-to-editor: v:string. Place text to editor
@alter-tbl: Node. Alter table node
@drop-action: sql:string.
@truncate-tbl: sql:string.
@use-db: qualified_name:string.
@gen-erd: Node.
@view-node-insights: Node. Either Schema or Table node.
@load-children: Node. Async event.
@on-dragging: Event.
@on-dragend: Event.
*/
import { mapState } from 'vuex'
import InsightViewer from '@wsModels/InsightViewer'
import AlterEditor from '@wsModels/AlterEditor'
import QueryTab from '@wsModels/QueryTab'
import QueryTabTmp from '@wsModels/QueryTabTmp'
import SchemaSidebar from '@wsModels/SchemaSidebar'
import customDragEvt from '@share/mixins/customDragEvt'
import asyncEmit from '@share/mixins/asyncEmit'
import schemaNodeHelper from '@wsSrc/utils/schemaNodeHelper'

export default {
    name: 'schema-tree-ctr',
    mixins: [customDragEvt, asyncEmit],
    props: {
        queryEditorId: { type: String, required: true },
        activeQueryTabId: { type: String, required: true },
        queryEditorTmp: { type: Object, required: true },
        activeQueryTabConn: { type: Object, required: true },
        filterTxt: { type: String, required: true },
        schemaSidebar: { type: Object, required: true },
    },
    data() {
        return {
            showCtxMenu: false,
            activeCtxNode: null,
            activeCtxItemOpts: [],
            hoveredNode: null,
        }
    },
    computed: {
        ...mapState({
            QUERY_MODES: state => state.mxsWorkspace.config.QUERY_MODES,
            QUERY_TAB_TYPES: state => state.mxsWorkspace.config.QUERY_TAB_TYPES,
            NODE_TYPES: state => state.mxsWorkspace.config.NODE_TYPES,
            NODE_GROUP_TYPES: state => state.mxsWorkspace.config.NODE_GROUP_TYPES,
            NODE_CTX_TYPES: state => state.mxsWorkspace.config.NODE_CTX_TYPES,
        }),
        activeQueryTab() {
            return QueryTab.find(this.activeQueryTabId) || {}
        },
        activeQueryTabType() {
            return this.$typy(this.activeQueryTab, 'type').safeString
        },
        isSqlEditor() {
            return this.activeQueryTabType === this.QUERY_TAB_TYPES.SQL_EDITOR
        },
        dbTreeData() {
            return this.$typy(this.queryEditorTmp, 'db_tree').safeArray
        },
        alterEditor() {
            return AlterEditor.find(this.activeQueryTab.id)
        },
        queryTabTmp() {
            return QueryTabTmp.find(this.activeQueryTab.id)
        },
        insightViewer() {
            return InsightViewer.find(this.activeQueryTab.id)
        },
        activeNode() {
            const { ALTER_EDITOR, INSIGHT_VIEWER, SQL_EDITOR } = this.QUERY_TAB_TYPES
            switch (this.activeQueryTabType) {
                case ALTER_EDITOR:
                    return this.$typy(this.alterEditor, 'active_node').safeObjectOrEmpty
                case INSIGHT_VIEWER:
                    return this.$typy(this.insightViewer, 'active_node').safeObjectOrEmpty
                case SQL_EDITOR:
                    return this.$typy(this.queryTabTmp, 'previewing_node').safeObjectOrEmpty
                default:
                    return null
            }
        },
        activeNodes: {
            get() {
                return [this.activeNode]
            },
            set(v) {
                if (v.length) {
                    const activeNode = this.$typy(this.minimizeNodes(v), '[0]').safeObjectOrEmpty
                    const { ALTER_EDITOR, INSIGHT_VIEWER, SQL_EDITOR } = this.QUERY_TAB_TYPES
                    switch (this.activeQueryTabType) {
                        case ALTER_EDITOR:
                            AlterEditor.update({
                                where: this.activeQueryTabId,
                                data: { active_node: activeNode },
                            })
                            break
                        case INSIGHT_VIEWER:
                            InsightViewer.update({
                                where: this.activeQueryTabId,
                                data: { active_node: activeNode },
                            })
                            break
                        case SQL_EDITOR:
                            QueryTabTmp.update({
                                where: this.activeQueryTabId,
                                data: { previewing_node: activeNode },
                            })
                            break
                    }
                }
            },
        },
        nodesHaveCtxMenu() {
            return Object.values(this.NODE_TYPES)
        },
        txtOpts() {
            return [
                {
                    text: this.$mxs_t('placeToEditor'),
                    children: this.genTxtOpts(this.NODE_CTX_TYPES.INSERT),
                },
                {
                    text: this.$mxs_t('copyToClipboard'),
                    children: this.genTxtOpts(this.NODE_CTX_TYPES.CLIPBOARD),
                },
            ]
        },
        // basic node options for different node types
        baseOptsMap() {
            const { SCHEMA, TBL, VIEW, SP, FN, COL, IDX, TRIGGER } = this.NODE_TYPES
            const {
                USE,
                VIEW_INSIGHTS,
                PRVW_DATA,
                PRVW_DATA_DETAILS,
                GEN_ERD,
            } = this.NODE_CTX_TYPES

            const previewOpts = [
                {
                    text: this.$mxs_t('previewData'),
                    type: PRVW_DATA,
                    disabled: !this.isSqlEditor,
                },
                {
                    text: this.$mxs_t('viewDetails'),
                    type: PRVW_DATA_DETAILS,
                    disabled: !this.isSqlEditor,
                },
            ]
            const spFnTriggerOpts = [
                { text: this.$mxs_t('showCreate'), type: VIEW_INSIGHTS },
                { divider: true },
                ...this.txtOpts,
            ]
            return {
                [SCHEMA]: [
                    { text: this.$mxs_t('useDb'), type: USE, disabled: !this.isSqlEditor },
                    { text: this.$mxs_t('viewInsights'), type: VIEW_INSIGHTS },
                    { text: this.$mxs_t('genErd'), type: GEN_ERD },
                    ...this.txtOpts,
                ],
                [TBL]: [
                    ...previewOpts,
                    { text: this.$mxs_t('viewInsights'), type: VIEW_INSIGHTS },
                    { divider: true },
                    ...this.txtOpts,
                ],
                [VIEW]: [
                    ...previewOpts,
                    { text: this.$mxs_t('showCreate'), type: VIEW_INSIGHTS },
                    { divider: true },
                    ...this.txtOpts,
                ],
                [SP]: spFnTriggerOpts,
                [FN]: spFnTriggerOpts,
                [COL]: this.txtOpts,
                [IDX]: this.txtOpts,
                [TRIGGER]: spFnTriggerOpts,
            }
        },
        expandedNodes: {
            get() {
                return this.$typy(this.schemaSidebar, 'expanded_nodes').safeArray
            },
            set(v) {
                let nodes = this.minimizeNodes(v)
                //   The order is important which is used to reload the schema and update the tree
                //   Sort expandedNodes by level property
                nodes.sort((a, b) => a.level - b.level)
                // Auto collapse all expanded nodes if schema node is collapsed
                let validLevels = nodes.map(node => node.level)
                if (validLevels[0] === 1)
                    SchemaSidebar.update({
                        where: this.queryEditorId,
                        data: {
                            expanded_nodes: nodes,
                        },
                    })
                else
                    SchemaSidebar.update({
                        where: this.queryEditorId,
                        data: {
                            expanded_nodes: [],
                        },
                    })
            },
        },
    },
    watch: {
        showCtxMenu(v) {
            if (!v) this.activeCtxNode = null
        },
    },
    methods: {
        filter(node, search, textKey) {
            return this.$helpers.ciStrIncludes(node[textKey], search)
        },
        showCtxBtn(node) {
            return Boolean(this.activeCtxNode && node.id === this.activeCtxNode.id)
        },
        /**
         * @param {Array} node - a node in db_tree_map
         * @returns {Array} minimized node
         */
        minimizeNode: ({ id, parentNameData, qualified_name, name, type, level }) => ({
            id,
            qualified_name,
            parentNameData,
            name,
            type,
            level,
        }),
        /**
         * @param {Array} nodes - array of nodes
         * @returns {Array} minimized nodes.
         */
        minimizeNodes(nodes) {
            return nodes.map(this.minimizeNode)
        },
        /**
         * @param {Object} node - a node in db_tree_map
         * @returns {Array} context options for non system node
         */
        genUserNodeOpts(node) {
            const { SCHEMA, TBL, VIEW, SP, FN, COL, IDX, TRIGGER } = this.NODE_TYPES
            const { DROP, ALTER, TRUNCATE } = this.NODE_CTX_TYPES
            const label = this.$helpers.capitalizeFirstLetter(node.type.toLowerCase())

            const dropOpt = { text: `${DROP} ${label}`, type: DROP }
            const alterOpt = { text: `${ALTER} ${label}`, type: ALTER }
            const truncateOpt = { text: `${TRUNCATE} ${label}`, type: TRUNCATE }

            switch (node.type) {
                case SCHEMA:
                case VIEW:
                case SP:
                case FN:
                case TRIGGER:
                    return [dropOpt]
                case TBL:
                    return [alterOpt, dropOpt, truncateOpt]
                case IDX:
                    return [dropOpt]
                case COL:
                default:
                    return []
            }
        },
        /**
         * Both INSERT and CLIPBOARD types have same options.
         * This generates txt options based on provided type
         * @param {String} type - INSERT OR CLIPBOARD
         * @returns {Array} - return context options
         */
        genTxtOpts(type) {
            return [
                { text: this.$mxs_t('qualifiedName'), type },
                { text: this.$mxs_t('nameQuoted'), type },
                { text: this.$mxs_t('name'), type },
            ]
        },
        genNodeOpts(node) {
            const baseOpts = this.baseOptsMap[node.type]
            let opts = baseOpts
            if (node.isSys) return opts
            const userNodeOpts = this.genUserNodeOpts(node)
            if (userNodeOpts.length) opts = [...opts, { divider: true }, ...userNodeOpts]
            return opts
        },
        /**
         * Both INSERT and CLIPBOARD types have same options.
         * This handles INSERT and CLIPBOARD options
         * @param {Object} node - node
         * @param {Object} opt - context menu option
         */
        handleTxtOpt({ node, opt }) {
            let v = ''
            switch (opt.text) {
                case this.$mxs_t('qualifiedName'):
                    v = node.qualified_name
                    break
                case this.$mxs_t('nameQuoted'):
                    v = this.$helpers.quotingIdentifier(node.name)
                    break
                case this.$mxs_t('name'):
                    v = node.name
                    break
            }
            const { INSERT, CLIPBOARD } = this.NODE_CTX_TYPES
            switch (opt.type) {
                case INSERT:
                    this.$emit('place-to-editor', v)
                    break
                case CLIPBOARD:
                    this.$helpers.copyTextToClipboard(v)
                    break
            }
        },
        handleOpenCtxMenu({ e, node }) {
            e.stopPropagation()
            if (this.$helpers.lodash.isEqual(this.activeCtxNode, node)) {
                this.showCtxMenu = false
                this.activeCtxNode = null
            } else {
                if (!this.showCtxMenu) this.showCtxMenu = true
                this.activeCtxNode = node
                this.activeCtxItemOpts = this.genNodeOpts(node)
            }
        },
        previewNode(node) {
            QueryTabTmp.update({
                where: this.activeQueryTabId,
                data: { previewing_node: this.minimizeNode(node) },
            })
            this.$emit('get-node-data', {
                query_mode: this.QUERY_MODES.PRVW_DATA,
                qualified_name: node.qualified_name,
            })
        },
        onNodeDblClick(node) {
            if (node.type === this.NODE_TYPES.SCHEMA && this.isSqlEditor)
                this.$emit('use-db', node.qualified_name)
        },
        onContextMenu({ e, item: node }) {
            if (this.nodesHaveCtxMenu.includes(node.type)) this.handleOpenCtxMenu({ e, node })
        },
        onNodeDragStart(e) {
            e.preventDefault()
            // Assign value to data in customDragEvt mixin
            this.isDragging = true
            this.dragTarget = e.target
        },
        async handleLoadChildren(node) {
            await this.asyncEmit('load-children', node)
        },
        /**
         * @param {Object} node - node
         * @param {Object} opt - context menu option
         */
        optionHandler({ node, opt }) {
            const {
                PRVW_DATA,
                PRVW_DATA_DETAILS,
                USE,
                INSERT,
                CLIPBOARD,
                DROP,
                ALTER,
                TRUNCATE,
                GEN_ERD,
                VIEW_INSIGHTS,
            } = this.NODE_CTX_TYPES

            const { quotingIdentifier: quoting } = this.$helpers
            const { TBL, IDX } = this.NODE_TYPES

            switch (opt.type) {
                case USE:
                    this.$emit('use-db', node.qualified_name)
                    break
                case PRVW_DATA:
                case PRVW_DATA_DETAILS:
                    this.activeNodes = [node] // updateActiveNode
                    this.$emit('get-node-data', {
                        query_mode: opt.type,
                        qualified_name: node.qualified_name,
                    })
                    break
                case INSERT:
                case CLIPBOARD:
                    this.handleTxtOpt({ node, opt })
                    break
                case DROP: {
                    let sql = `DROP ${node.type} ${node.qualified_name};`
                    if (node.type === IDX) {
                        const db = schemaNodeHelper.getSchemaName(node)
                        const tbl = schemaNodeHelper.getTblName(node)
                        const target = `${quoting(db)}.${quoting(tbl)}`
                        sql = `DROP ${node.type} ${quoting(node.name)} ON ${target};`
                    }
                    this.$emit('drop-action', sql)
                    break
                }
                case ALTER:
                    if (node.type === TBL) this.$emit('alter-tbl', this.minimizeNode(node))
                    break
                case TRUNCATE:
                    if (node.type === TBL)
                        this.$emit('truncate-tbl', `TRUNCATE TABLE ${node.qualified_name};`)
                    break
                case GEN_ERD:
                    this.$emit('gen-erd', this.minimizeNode(node))
                    break
                case VIEW_INSIGHTS:
                    this.$emit('view-node-insights', this.minimizeNode(node))
                    break
            }
        },
    },
}
</script>
<style lang="scss" scoped>
.node-tooltip {
    font-size: 0.75rem;
}
</style>
<style lang="scss">
.mxs-treeview {
    .v-treeview-node__toggle {
        width: 16px;
        height: 16px;
    }
    .v-treeview-node__level {
        width: 16px;
    }
    .v-treeview-node__root {
        min-height: 32px;
    }
}
</style>
