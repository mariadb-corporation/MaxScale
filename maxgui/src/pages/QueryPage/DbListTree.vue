<template>
    <!-- TODO: Virtual scroll treeview -->
    <div v-if="getDbTreeData.length">
        <m-treeview
            :items="getDbTreeData"
            :search="$parent.searchSchema"
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
                        class="text-truncate d-inline-block"
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
                    <v-icon size="12" color="deep-ocean">more_horiz</v-icon>
                </v-btn>
            </template>
        </m-treeview>
        <v-tooltip
            v-if="hoveredItem && nodesHaveCtxMenu.includes(hoveredItem.type)"
            :value="Boolean(hoveredItem)"
            :disabled="isDragging"
            right
            :nudge-right="45"
            transition="slide-x-transition"
            content-class="shadow-drop"
            :activator="`#node-tooltip-activator-${hoveredItem.key}`"
        >
            <table class="node-tooltip color text-text py-2 px-4">
                <tbody>
                    <tr v-for="(value, key) in hoveredItem.data" :key="key">
                        <td class="font-weight-bold pr-2">{{ key }}:</td>
                        <td>{{ value }}</td>
                    </tr>
                </tbody>
            </table>
        </v-tooltip>
        <sub-menu
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
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-10-29
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapGetters, mapMutations, mapState } from 'vuex'
import customDragEvt from 'mixins/customDragEvt'
export default {
    name: 'db-list-tree',
    mixins: [customDragEvt],
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
            SQL_DDL_ALTER_SPECS: state => state.app_config.SQL_DDL_ALTER_SPECS,
            SQL_EDITOR_MODES: state => state.app_config.SQL_EDITOR_MODES,
            SQL_NODE_TYPES: state => state.app_config.SQL_NODE_TYPES,
            SQL_NODE_CTX_OPT_TYPES: state => state.app_config.SQL_NODE_CTX_OPT_TYPES,
            expanded_nodes: state => state.query.expanded_nodes,
            active_wke_id: state => state.query.active_wke_id,
            active_db: state => state.query.active_db,
        }),
        ...mapGetters({
            getDbTreeData: 'query/getDbTreeData',
            getActiveTreeNode: 'query/getActiveTreeNode',
            getAlteredActiveNode: 'query/getAlteredActiveNode',
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
                { text: this.$t('previewData'), type: QUERY },
                { text: this.$t('viewDetails'), type: QUERY },
            ]
        },
        insertOpts() {
            const {
                TXT_EDITOR: { INSERT },
            } = this.SQL_NODE_CTX_OPT_TYPES
            return [
                {
                    text: this.$t('placeToEditor'),
                    children: this.genTxtOpts(INSERT),
                },
            ]
        },
        clipboardOpts() {
            const { CLIPBOARD } = this.SQL_NODE_CTX_OPT_TYPES
            return [
                {
                    text: this.$t('copyToClipboard'),
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
                    { text: this.$t('useDb'), type: USE },
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
                [SCHEMA]: [{ text: this.$t('dropSchema'), type: DD }],
                [TABLE]: [
                    { text: this.$t('alterTbl'), type: DD },
                    { text: this.$t('dropTbl'), type: DD },
                    { text: this.$t('truncateTbl'), type: DD },
                ],
                [SP]: [{ text: this.$t('dropSp'), type: DD }],
                [COL]: [],
                [TRIGGER]: [{ text: this.$t('dropTrigger'), type: DD }],
            }
        },
        filter() {
            return (item, search, textKey) => item[textKey].indexOf(search) > -1
        },
        showCtxBtn() {
            return item => this.activeCtxItem && item.id === this.activeCtxItem.id
        },
        // Use either getActiveTreeNode or getAlteredActiveNode
        activeNodes: {
            get() {
                let nodes = []
                if (this.$typy(this.getAlteredActiveNode, 'id').safeString)
                    nodes = [...nodes, this.getAlteredActiveNode]
                else if (this.$typy(this.getActiveTreeNode, 'id').safeString)
                    nodes = [...nodes, this.getActiveTreeNode]
                return nodes
            },
            set(v) {
                if (v.length) {
                    const activeNodes = this.minimizeNodes(v)
                    if (this.$typy(this.getAlteredActiveNode, 'id').safeString) {
                        this.UPDATE_TBL_CREATION_INFO_MAP({
                            id: this.active_wke_id,
                            payload: {
                                altered_active_node: activeNodes[0],
                            },
                        })
                    } else
                        this.UPDATE_DB_TREE_MAP({
                            id: this.active_wke_id,
                            payload: {
                                active_tree_node: activeNodes[0],
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
        this.addExpandedNodesWatcher()
    },
    deactivated() {
        this.rmExpandedNodesWatcher()
    },
    methods: {
        ...mapMutations({
            UPDATE_DB_TREE_MAP: 'query/UPDATE_DB_TREE_MAP',
            SET_EXPANDED_NODES: 'query/SET_EXPANDED_NODES',
            UPDATE_CURR_EDITOR_MODE_MAP: 'query/UPDATE_CURR_EDITOR_MODE_MAP',
            SET_CURR_DDL_COL_SPEC: 'query/SET_CURR_DDL_COL_SPEC',
            UPDATE_TBL_CREATION_INFO_MAP: 'query/UPDATE_TBL_CREATION_INFO_MAP',
        }),
        addExpandedNodesWatcher() {
            this.rmExpandedNodesWatcher = this.$watch(
                'expandedNodes',
                (v, oV) => {
                    const oldNodeIds = oV.map(node => node.id)
                    const newNodeIds = v.map(node => node.id)
                    if (!this.$help.lodash.isEqual(newNodeIds, oldNodeIds)) {
                        let nodes = this.minimizeNodes(v)
                        //   The order is important which is used to reload the schema and update the tree
                        //   Sort expandedNodes by level property
                        nodes.sort((a, b) => a.level - b.level)
                        // Auto collapse all expanded nodes if schema node is collapsed
                        let validLevels = nodes.map(node => node.level)
                        if (validLevels[0] === 0) this.SET_EXPANDED_NODES(nodes)
                        else this.SET_EXPANDED_NODES([])
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
            await this.emitPromise('load-children', item)
        },
        async emitPromise(method, ...params) {
            const listener = this.$listeners[method]
            if (listener) {
                const res = await listener(...params)
                return res === undefined || res
            }
            return false
        },
        handleOpenCtxMenu({ e, item }) {
            e.stopPropagation()
            if (this.$help.lodash.isEqual(this.activeCtxItem, item)) {
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
                { text: this.$t('qualifiedNameQuoted'), type },
                { text: this.$t('qualifiedName'), type },
                { text: this.$t('nameQuoted'), type },
                { text: this.$t('name'), type },
            ]
        },

        /**
         * @param {Object} item - node
         * @param {Object} opt - context menu option
         */
        handleEmitQueryOpt({ item, opt }) {
            /**
             * If altered_active_node exists, clear it first so that
             * activeNodes can be updated
             */
            if (this.$typy(this.getAlteredActiveNode, 'id').safeString)
                // Clear altered active node
                this.UPDATE_TBL_CREATION_INFO_MAP({
                    id: this.active_wke_id,
                    payload: {
                        altered_active_node: null,
                    },
                })
            this.updateActiveNode(item)
            switch (opt.text) {
                case this.$t('previewData'):
                    this.$emit('preview-data', item.id)
                    break
                case this.$t('viewDetails'):
                    this.$emit('view-details', item.id)
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
                case this.$t('qualifiedNameQuoted'):
                    v = this.$help.escapeIdentifiers(item.id)
                    break
                case this.$t('qualifiedName'):
                    v = item.id
                    break
                case this.$t('nameQuoted'):
                    v = this.$help.escapeIdentifiers(item.name)
                    break
                case this.$t('name'):
                    v = item.name
                    break
            }
            switch (opt.type) {
                case INSERT:
                    this.$emit('place-to-editor', v)
                    break
                case CLIPBOARD:
                    this.$help.copyTextToClipboard(v)
                    break
            }
        },
        /**
         * @param {Object} item - node
         * @param {Object} opt - context menu option
         */
        handleEmitDD_opt({ item, opt }) {
            switch (opt.text) {
                case this.$t('alterTbl'):
                    {
                        const alterActiveNode = {
                            id: item.id,
                            type: item.type,
                            level: item.level,
                        }
                        this.UPDATE_TBL_CREATION_INFO_MAP({
                            id: this.active_wke_id,
                            payload: {
                                altered_active_node: alterActiveNode,
                            },
                        })
                        this.UPDATE_CURR_EDITOR_MODE_MAP({
                            id: this.active_wke_id,
                            payload: this.SQL_EDITOR_MODES.DDL_EDITOR,
                        })
                        this.SET_CURR_DDL_COL_SPEC(this.SQL_DDL_ALTER_SPECS.COLUMNS)
                        this.$emit('alter-tbl', alterActiveNode)
                    }
                    break
                case this.$t('dropTbl'):
                case this.$t('dropSchema'):
                case this.$t('dropSp'):
                case this.$t('dropTrigger'):
                    this.$emit('drop-action', { id: item.id, type: item.type })
                    break
                case this.$t('truncateTbl'):
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
            this.UPDATE_CURR_EDITOR_MODE_MAP({
                id: this.active_wke_id,
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
                    return '$vuetify.icons.database'
                //TODO: a separate icon for Tables
                case TABLES:
                    return '$vuetify.icons.table'
                case SPS:
                    return '$vuetify.icons.storedProcedures'
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
            if (item.canBeHighlighted) this.$emit('preview-data', this.activeNodes[0].id)
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
::v-deep .v-treeview-node__toggle {
    width: 16px;
    height: 16px;
}
::v-deep .v-treeview-node__level {
    width: 16px;
}
.node-label {
    height: 40px;
}
.node-tooltip {
    font-size: 0.75rem;
}
</style>
