<template>
    <!-- TODO: Virtual scroll treeview -->
    <div v-if="getDbTreeData.length">
        <mxs-treeview
            class="mxs-treeview"
            :items="getDbTreeData"
            :search="search_schema"
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
            @item:hovered="hoveredItem = $event"
            @item:dblclick="onNodeDblClick"
        >
            <template v-slot:label="{ item }">
                <div
                    :id="`node-tooltip-activator-${item.key}`"
                    class="d-flex align-center node-label"
                    :class="{ 'cursor--grab': item.draggable }"
                    @mousedown="item.draggable ? onNodeDragStart($event) : null"
                >
                    <v-icon class="mr-1" size="12" color="deep-ocean">
                        {{ iconSheet(item) }}
                    </v-icon>
                    <span
                        v-mxs-highlighter="{ keyword: search_schema, txt: item.name }"
                        class="text-truncate d-inline-block node-name"
                        :class="{
                            'font-weight-bold':
                                item.type === SQL_NODE_TYPES.SCHEMA && active_db === item.name,
                        }"
                    >
                        {{ item.name }}
                    </span>
                </div>
            </template>
            <template v-slot:append="{ isHover, item }">
                <v-btn
                    v-show="nodesHaveCtxMenu.includes(item.type) && (isHover || showCtxBtn(item))"
                    :id="`ctx-menu-activator-${item.key}`"
                    icon
                    x-small
                    @click="e => handleOpenCtxMenu({ e, item })"
                >
                    <v-icon size="12" color="deep-ocean">mdi-dots-horizontal</v-icon>
                </v-btn>
            </template>
        </mxs-treeview>
        <v-tooltip
            v-if="hoveredItem && nodesHaveCtxMenu.includes(hoveredItem.type)"
            :value="Boolean(hoveredItem)"
            :disabled="isDragging"
            right
            :nudge-right="45"
            transition="slide-x-transition"
            content-class="shadow-drop mxs-color-helper white text-text py-2 px-4"
            :activator="`#node-tooltip-activator-${hoveredItem.key}`"
        >
            <table class="node-tooltip">
                <tbody>
                    <tr v-for="(value, key) in hoveredItem.data" :key="key">
                        <td class="font-weight-bold pr-2">{{ key }}:</td>
                        <td>{{ value }}</td>
                    </tr>
                </tbody>
            </table>
        </v-tooltip>
        <mxs-sub-menu
            v-if="activeCtxItem"
            :key="activeCtxItem.key"
            v-model="showCtxMenu"
            left
            :nudge-right="12"
            :nudge-bottom="10"
            :items="getNodeOpts(activeCtxItem)"
            :activator="`#ctx-menu-activator-${activeCtxItem.key}`"
            @item-click="optionHandler({ item: activeCtxItem, opt: $event })"
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
 * Change Date: 2027-05-22
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
/*
This component emits the following events
@get-node-data: { SQL_QUERY_MODE: string, schemaId:string }
@place-to-editor: v:string. Place text to editor
@alter-tbl: Node. Alter table node
@drop-action: { id:string, type:string }: Node.
@truncate-tbl: { id:string }: Node.
@use-db: { id:string }: Node.
@load-children: Node. Async event.
@on-dragging: Event.
@on-dragend: Event.
*/
import { mapGetters, mapMutations, mapState } from 'vuex'
import customDragEvt from '@share/mixins/customDragEvt'
import asyncEmit from '@share/mixins/asyncEmit'
export default {
    name: 'schema-tree-ctr',
    mixins: [customDragEvt, asyncEmit],
    data() {
        return {
            showCtxMenu: false,
            activeCtxItem: null, // active item to show in context(options) menu
            hoveredItem: null,
            expandedNodes: [],
        }
    },
    computed: {
        ...mapState({
            SQL_QUERY_MODES: state => state.queryEditorConfig.config.SQL_QUERY_MODES,
            SQL_EDITOR_MODES: state => state.queryEditorConfig.config.SQL_EDITOR_MODES,
            SQL_NODE_TYPES: state => state.queryEditorConfig.config.SQL_NODE_TYPES,
            SQL_NODE_CTX_OPT_TYPES: state => state.queryEditorConfig.config.SQL_NODE_CTX_OPT_TYPES,
            expanded_nodes: state => state.schemaSidebar.expanded_nodes,
            active_wke_id: state => state.wke.active_wke_id,
            active_db: state => state.queryConn.active_db,
            search_schema: state => state.schemaSidebar.search_schema,
            tbl_creation_info: state => state.editor.tbl_creation_info,
        }),
        ...mapGetters({
            getDbTreeData: 'schemaSidebar/getDbTreeData',
            getActivePrvwTblNode: 'schemaSidebar/getActivePrvwTblNode',
            getAlteredActiveNode: 'editor/getAlteredActiveNode',
            getActiveSessionId: 'querySession/getActiveSessionId',
        }),
        nodesHaveCtxMenu() {
            const { SCHEMA, TABLE, SP, COL, TRIGGER } = this.SQL_NODE_TYPES
            return [SCHEMA, TABLE, SP, COL, TRIGGER]
        },
        queryOpts() {
            const {
                TXT_EDITOR: { QUERY },
            } = this.SQL_NODE_CTX_OPT_TYPES
            return [
                { text: this.$mxs_t('previewData'), type: QUERY },
                { text: this.$mxs_t('viewDetails'), type: QUERY },
            ]
        },
        insertOpts() {
            const {
                TXT_EDITOR: { INSERT },
            } = this.SQL_NODE_CTX_OPT_TYPES
            return [
                {
                    text: this.$mxs_t('placeToEditor'),
                    children: this.genTxtOpts(INSERT),
                },
            ]
        },
        clipboardOpts() {
            const { CLIPBOARD } = this.SQL_NODE_CTX_OPT_TYPES
            return [
                {
                    text: this.$mxs_t('copyToClipboard'),
                    children: this.genTxtOpts(CLIPBOARD),
                },
            ]
        },
        txtEditorRelatedOpts() {
            return [...this.queryOpts, { divider: true }, ...this.insertOpts]
        },
        // basic node options for different node types
        baseOptsMap() {
            const { SCHEMA, TABLE, SP, COL, TRIGGER } = this.SQL_NODE_TYPES
            const {
                ADMIN: { USE },
            } = this.SQL_NODE_CTX_OPT_TYPES
            return {
                [SCHEMA]: [
                    { text: this.$mxs_t('useDb'), type: USE },
                    ...this.insertOpts,
                    ...this.clipboardOpts,
                ],
                [TABLE]: [...this.txtEditorRelatedOpts, ...this.clipboardOpts],
                [SP]: [...this.insertOpts, ...this.clipboardOpts],
                [COL]: [...this.insertOpts, ...this.clipboardOpts],
                [TRIGGER]: [...this.insertOpts, ...this.clipboardOpts],
            }
        },
        // more node options for user's nodes
        userNodeOptsMap() {
            const { SCHEMA, TABLE, SP, COL, TRIGGER } = this.SQL_NODE_TYPES
            const {
                DDL: { DD },
            } = this.SQL_NODE_CTX_OPT_TYPES
            return {
                [SCHEMA]: [{ text: this.$mxs_t('dropSchema'), type: DD }],
                [TABLE]: [
                    { text: this.$mxs_t('alterTbl'), type: DD },
                    { text: this.$mxs_t('dropTbl'), type: DD },
                    { text: this.$mxs_t('truncateTbl'), type: DD },
                ],
                [SP]: [{ text: this.$mxs_t('dropSp'), type: DD }],
                [COL]: [],
                [TRIGGER]: [{ text: this.$mxs_t('dropTrigger'), type: DD }],
            }
        },
        // Use either getActivePrvwTblNode or getAlteredActiveNode
        activeNodes: {
            get() {
                let nodes = []
                if (this.$typy(this.getAlteredActiveNode, 'id').safeString)
                    nodes = [...nodes, this.getAlteredActiveNode]
                else if (this.$typy(this.getActivePrvwTblNode, 'id').safeString)
                    nodes = [...nodes, this.getActivePrvwTblNode]
                return nodes
            },
            set(v) {
                if (v.length) {
                    const activeNodes = this.minimizeNodes(v)
                    if (this.$typy(this.getAlteredActiveNode, 'id').safeString) {
                        this.SET_TBL_CREATION_INFO({
                            id: this.getActiveSessionId,
                            payload: {
                                ...this.tbl_creation_info,
                                altered_active_node: activeNodes[0],
                            },
                        })
                    } else
                        this.PATCH_DB_TREE_MAP({
                            id: this.active_wke_id,
                            payload: {
                                active_prvw_tbl_node: activeNodes[0],
                            },
                        })
                }
            },
        },
    },
    watch: {
        showCtxMenu(v) {
            if (!v) this.activeCtxItem = null
        },
    },
    activated() {
        this.expandedNodes = this.expanded_nodes
        this.watch_expandedNodes()
    },
    deactivated() {
        this.$typy(this.unwatch_expandedNodes).safeFunction()
    },
    methods: {
        ...mapMutations({
            PATCH_DB_TREE_MAP: 'schemaSidebar/PATCH_DB_TREE_MAP',
            SET_EXPANDED_NODES: 'schemaSidebar/SET_EXPANDED_NODES',
            SET_CURR_EDITOR_MODE: 'editor/SET_CURR_EDITOR_MODE',
            SET_TBL_CREATION_INFO: 'editor/SET_TBL_CREATION_INFO',
        }),
        filter(item, search, textKey) {
            return this.$helpers.ciStrIncludes(item[textKey], search)
        },
        showCtxBtn(item) {
            return Boolean(this.activeCtxItem && item.id === this.activeCtxItem.id)
        },
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
                        if (validLevels[0] === 0)
                            this.SET_EXPANDED_NODES({
                                payload: nodes,
                                id: this.active_wke_id,
                            })
                        else
                            this.SET_EXPANDED_NODES({
                                payload: [],
                                id: this.active_wke_id,
                            })
                    }
                },
                { deep: true }
            )
        },
        /**
         * @param {Array} nodes - array of nodes
         * @returns {Array} minimized nodes where each node is an object with id and type props
         */
        minimizeNodes: nodes =>
            nodes.map(node => ({ id: node.id, type: node.type, level: node.level })),

        async handleLoadChildren(item) {
            await this.asyncEmit('load-children', item)
        },
        handleOpenCtxMenu({ e, item }) {
            e.stopPropagation()
            if (this.$helpers.lodash.isEqual(this.activeCtxItem, item)) {
                this.showCtxMenu = false
                this.activeCtxItem = null
            } else {
                if (!this.showCtxMenu) this.showCtxMenu = true
                this.activeCtxItem = item
            }
        },
        updateActiveNode(item) {
            this.activeNodes = [item]
        },

        /**
         * Both INSERT and CLIPBOARD types have same options.
         * This generates txt options based on provided type
         * @param {String} type - INSERT OR CLIPBOARD
         * @returns {Array} - return context options
         */
        genTxtOpts(type) {
            return [
                { text: this.$mxs_t('qualifiedNameQuoted'), type },
                { text: this.$mxs_t('qualifiedName'), type },
                { text: this.$mxs_t('nameQuoted'), type },
                { text: this.$mxs_t('name'), type },
            ]
        },

        /**
         * @param {Object} item - node
         * @param {Object} opt - context menu option
         */
        handleEmitQueryOpt({ item, opt }) {
            this.updateActiveNode(item)
            switch (opt.text) {
                case this.$mxs_t('previewData'):
                    this.$emit('get-node-data', {
                        SQL_QUERY_MODE: this.SQL_QUERY_MODES.PRVW_DATA,
                        schemaId: item.id,
                    })
                    break
                case this.$mxs_t('viewDetails'):
                    this.$emit('get-node-data', {
                        SQL_QUERY_MODE: this.SQL_QUERY_MODES.PRVW_DATA_DETAILS,
                        schemaId: item.id,
                    })
                    break
            }
        },
        /**
         * Both INSERT and CLIPBOARD types have same options.
         * This handles INSERT and CLIPBOARD options
         * @param {Object} item - node
         * @param {Object} opt - context menu option
         */
        handleTxtOpt({ item, opt }) {
            const {
                CLIPBOARD,
                TXT_EDITOR: { INSERT },
            } = this.SQL_NODE_CTX_OPT_TYPES
            let v = ''
            switch (opt.text) {
                case this.$mxs_t('qualifiedNameQuoted'):
                    v = this.$helpers.escapeIdentifiers(item.id)
                    break
                case this.$mxs_t('qualifiedName'):
                    v = item.id
                    break
                case this.$mxs_t('nameQuoted'):
                    v = this.$helpers.escapeIdentifiers(item.name)
                    break
                case this.$mxs_t('name'):
                    v = item.name
                    break
            }
            switch (opt.type) {
                case INSERT:
                    this.$emit('place-to-editor', v)
                    break
                case CLIPBOARD:
                    this.$helpers.copyTextToClipboard(v)
                    break
            }
        },
        /**
         * @param {Object} item - node
         * @param {Object} opt - context menu option
         */
        handleEmitDD_opt({ item, opt }) {
            switch (opt.text) {
                case this.$mxs_t('alterTbl'):
                    this.$emit('alter-tbl', {
                        id: item.id,
                        type: item.type,
                        level: item.level,
                        name: item.name,
                    })
                    break
                case this.$mxs_t('dropTbl'):
                case this.$mxs_t('dropSchema'):
                case this.$mxs_t('dropSp'):
                case this.$mxs_t('dropTrigger'):
                    this.$emit('drop-action', { id: item.id, type: item.type })
                    break
                case this.$mxs_t('truncateTbl'):
                    this.$emit('truncate-tbl', item.id)
                    break
            }
        },
        /**
         * @param {Object} item - node
         * @param {Object} opt - context menu option
         */
        handleTxtEditorOpt({ item, opt }) {
            const {
                TXT_EDITOR: { INSERT, QUERY },
            } = this.SQL_NODE_CTX_OPT_TYPES
            this.SET_CURR_EDITOR_MODE({
                id: this.getActiveSessionId,
                payload: this.SQL_EDITOR_MODES.TXT_EDITOR,
            })
            switch (opt.type) {
                case QUERY:
                    this.handleEmitQueryOpt({ item, opt })
                    break
                case INSERT:
                    this.handleTxtOpt({ item, opt })
                    break
            }
        },
        /**
         * @param {Object} item - node
         * @param {Object} opt - context menu option
         */
        optionHandler({ item, opt }) {
            const {
                CLIPBOARD,
                TXT_EDITOR: { INSERT, QUERY },
                DDL: { DD },
                ADMIN: { USE },
            } = this.SQL_NODE_CTX_OPT_TYPES
            switch (opt.type) {
                case DD:
                    this.handleEmitDD_opt({ item, opt })
                    break
                case USE:
                    this.$emit('use-db', item.id)
                    break
                case INSERT:
                case QUERY:
                    this.handleTxtEditorOpt({ item, opt })
                    break
                case CLIPBOARD:
                    this.handleTxtOpt({ item, opt })
                    break
            }
        },
        iconSheet(item) {
            const { SCHEMA, TABLES, SPS } = this.SQL_NODE_TYPES
            switch (item.type) {
                case SCHEMA:
                    return '$vuetify.icons.mxs_database'
                //TODO: a separate icon for Tables
                case TABLES:
                    return '$vuetify.icons.mxs_table'
                case SPS:
                    return '$vuetify.icons.mxs_storedProcedures'
                //TODO: an icon for Column
            }
        },
        getNodeOpts(node) {
            if (node.isSys) return this.baseOptsMap[node.type]
            return [
                ...this.baseOptsMap[node.type],
                { divider: true },
                ...this.userNodeOptsMap[node.type],
            ]
        },
        onNodeClick(item) {
            if (item.canBeHighlighted)
                this.$emit('get-node-data', {
                    SQL_QUERY_MODE: this.SQL_QUERY_MODES.PRVW_DATA,
                    schemaId: this.activeNodes[0].id,
                })
        },
        onNodeDblClick(item) {
            if (item.type === this.SQL_NODE_TYPES.SCHEMA) this.$emit('use-db', item.id)
        },
        onContextMenu({ e, item }) {
            if (this.nodesHaveCtxMenu.includes(item.type)) this.handleOpenCtxMenu({ e, item })
        },
        onNodeDragStart(e) {
            e.preventDefault()
            // Assign value to data in customDragEvt mixin
            this.isDragging = true
            this.dragTarget = e.target
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
