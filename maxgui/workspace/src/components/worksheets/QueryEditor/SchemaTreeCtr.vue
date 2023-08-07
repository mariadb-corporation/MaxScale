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
            @item:click="onNodeClick"
            @item:contextmenu="onContextMenu"
            @item:hovered="hoveredNode = $event"
            @item:dblclick="onNodeDblClick"
        >
            <template v-slot:label="{ item: node }">
                <div
                    :id="`node-tooltip-activator-${node.key}`"
                    class="d-flex align-center node-label"
                    :class="{ 'cursor--grab': node.draggable }"
                    @mousedown="node.draggable ? onNodeDragStart($event) : null"
                >
                    <v-icon class="mr-1" size="12" color="blue-azure">
                        {{ iconSheet(node) }}
                    </v-icon>
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
                </div>
            </template>
            <template v-slot:append="{ isHover, item: node }">
                <v-btn
                    v-show="nodesHaveCtxMenu.includes(node.type) && (isHover || showCtxBtn(node))"
                    :id="`ctx-menu-activator-${node.key}`"
                    icon
                    x-small
                    @click="e => handleOpenCtxMenu({ e, node })"
                >
                    <v-icon size="12" color="navigation">mdi-dots-horizontal</v-icon>
                </v-btn>
            </template>
        </mxs-treeview>
        <v-tooltip
            v-if="hoveredNode && nodesHaveCtxMenu.includes(hoveredNode.type)"
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
            left
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
 * Change Date: 2027-07-24
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
@load-children: Node. Async event.
@on-dragging: Event.
@on-dragend: Event.
*/
import { mapState } from 'vuex'
import Editor from '@wsModels/Editor'
import QueryConn from '@wsModels/QueryConn'
import QueryEditor from '@wsModels/QueryEditor'
import QueryTabTmp from '@wsModels/QueryTabTmp'
import SchemaSidebar from '@wsModels/SchemaSidebar'
import customDragEvt from '@share/mixins/customDragEvt'
import asyncEmit from '@share/mixins/asyncEmit'
import queryHelper from '@wsSrc/store/queryHelper'

export default {
    name: 'schema-tree-ctr',
    mixins: [customDragEvt, asyncEmit],
    data() {
        return {
            showCtxMenu: false,
            activeCtxNode: null,
            activeCtxItemOpts: [],
            hoveredNode: null,
            expandedNodes: [],
        }
    },
    computed: {
        ...mapState({
            QUERY_MODES: state => state.mxsWorkspace.config.QUERY_MODES,
            EDITOR_MODES: state => state.mxsWorkspace.config.EDITOR_MODES,
            NODE_TYPES: state => state.mxsWorkspace.config.NODE_TYPES,
            NODE_GROUP_TYPES: state => state.mxsWorkspace.config.NODE_GROUP_TYPES,
            NODE_CTX_TYPES: state => state.mxsWorkspace.config.NODE_CTX_TYPES,
        }),
        queryEditorId() {
            return QueryEditor.getters('getQueryEditorId')
        },
        activeQueryTabConn() {
            return QueryConn.getters('getActiveQueryTabConn')
        },
        activeQueryTabId() {
            return QueryEditor.getters('getActiveQueryTabId')
        },
        filterTxt() {
            return SchemaSidebar.getters('getFilterTxt')
        },
        dbTreeData() {
            return SchemaSidebar.getters('getDbTreeData')
        },
        activePrvwNode() {
            return SchemaSidebar.getters('getActivePrvwNode')
        },
        alteredActiveNode() {
            return Editor.getters('getAlteredActiveNode')
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
            const { USE, PRVW_DATA, PRVW_DATA_DETAILS } = this.NODE_CTX_TYPES

            const tblViewOpts = [
                { text: this.$mxs_t('previewData'), type: PRVW_DATA },
                { text: this.$mxs_t('viewDetails'), type: PRVW_DATA_DETAILS },
                { divider: true },
                ...this.txtOpts,
            ]
            return {
                [SCHEMA]: [{ text: this.$mxs_t('useDb'), type: USE }, ...this.txtOpts],
                [TBL]: tblViewOpts,
                [VIEW]: tblViewOpts,
                [SP]: this.txtOpts,
                [FN]: this.txtOpts,
                [COL]: this.txtOpts,
                [IDX]: this.txtOpts,
                [TRIGGER]: this.txtOpts,
            }
        },
        // Use either activePrvwNode or alteredActiveNode
        activeNodes: {
            get() {
                let nodes = []
                if (this.$typy(this.alteredActiveNode, 'id').safeString)
                    nodes = [...nodes, this.alteredActiveNode]
                else if (this.$typy(this.activePrvwNode, 'id').safeString)
                    nodes = [...nodes, this.activePrvwNode]
                return nodes
            },
            set(v) {
                if (v.length) {
                    const activeNodes = this.minimizeNodes(v)
                    if (this.$typy(this.alteredActiveNode, 'id').safeString)
                        Editor.update({
                            where: this.activeQueryTabId,
                            data(editor) {
                                editor.tbl_creation_info.altered_active_node = activeNodes[0]
                            },
                        })
                    else
                        QueryTabTmp.update({
                            where: this.activeQueryTabId,
                            data: {
                                active_prvw_node: activeNodes[0],
                            },
                        })
                }
            },
        },
    },
    watch: {
        showCtxMenu(v) {
            if (!v) this.activeCtxNode = null
        },
    },
    activated() {
        this.expandedNodes = SchemaSidebar.getters('getExpandedNodes')
        this.watch_expandedNodes()
    },
    deactivated() {
        this.$typy(this.unwatch_expandedNodes).safeFunction()
    },
    methods: {
        watch_expandedNodes() {
            this.unwatch_expandedNodes = this.$watch(
                'expandedNodes',
                (v, oV) => {
                    const oldNodeIds = oV.map(node => node.id)
                    const newNodeIds = v.map(node => node.id)
                    if (!this.$helpers.lodash.isEqual(newNodeIds, oldNodeIds)) {
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
                    }
                },
                { deep: true }
            )
        },
        filter(node, search, textKey) {
            return this.$helpers.ciStrIncludes(node[textKey], search)
        },
        showCtxBtn(node) {
            return Boolean(this.activeCtxNode && node.id === this.activeCtxNode.id)
        },
        iconSheet(node) {
            const { SCHEMA } = this.NODE_TYPES
            const { TBL_G, VIEW_G, SP_G, FN_G } = this.NODE_GROUP_TYPES
            switch (node.type) {
                case SCHEMA:
                    return '$vuetify.icons.mxs_database'
                case TBL_G:
                    return '$vuetify.icons.mxs_table'
                case VIEW_G:
                    return 'mdi-view-dashboard-outline'
                case SP_G:
                    return '$vuetify.icons.mxs_storedProcedures'
                case FN_G:
                    return 'mdi-function-variant'
                //TODO: find icons for COL_G, TRIGGER_G
            }
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
        onNodeClick(node) {
            if (node.activatable)
                this.$emit('get-node-data', {
                    query_mode: this.QUERY_MODES.PRVW_DATA,
                    qualified_name: node.qualified_name,
                })
        },
        onNodeDblClick(node) {
            if (node.type === this.NODE_TYPES.SCHEMA) this.$emit('use-db', node.qualified_name)
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
            } = this.NODE_CTX_TYPES

            const { quotingIdentifier: quoting } = this.$helpers
            const { TBL, IDX } = this.NODE_TYPES

            switch (opt.type) {
                case USE:
                    this.$emit('use-db', node.qualified_name)
                    break
                case PRVW_DATA:
                case PRVW_DATA_DETAILS:
                    Editor.update({
                        where: this.activeQueryTabId,
                        data: {
                            curr_editor_mode: this.EDITOR_MODES.TXT_EDITOR,
                        },
                    })
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
                        const db = queryHelper.getSchemaName(node)
                        const tbl = queryHelper.getTblName(node)
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
            }
        },
    },
}
</script>
<style lang="scss" scoped>
.node-label {
    height: 40px;
}
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
}
</style>
