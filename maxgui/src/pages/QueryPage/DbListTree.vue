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
                    <span class="text-truncate d-inline-block">
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
        <v-menu
            v-if="activeCtxItem"
            :key="activeCtxItem.key"
            v-model="showCtxMenu"
            transition="slide-y-transition"
            left
            nudge-right="12"
            nudge-bottom="28"
            content-class="mariadb-select-v-menu mariadb-select-v-menu--full-border"
            :activator="`#ctx-menu-activator-${activeCtxItem.key}`"
        >
            <v-list v-for="option in getNodeOpts(activeCtxItem)" :key="option">
                <v-list-item
                    dense
                    link
                    @click="() => optionHandler({ item: activeCtxItem, option })"
                >
                    <v-list-item-title class="color text-text" v-text="option" />
                </v-list-item>
            </v-list>
        </v-menu>
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
            /**
             *  TODO: Refactor and dry ctx menu. A menu option named `Insert to editor`
             *  has sub-menu `insertSchemaToEditor` and `insertNameToEditor`
             */
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
            expanded_nodes: state => state.query.expanded_nodes,
            active_wke_id: state => state.query.active_wke_id,
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
        // basic node options for different node types
        baseOptsMap() {
            const { SCHEMA, TABLE, SP, COL, TRIGGER } = this.SQL_NODE_TYPES
            return {
                [SCHEMA]: [this.$t('useDb'), this.$t('placeSchemaInEditor')],
                [TABLE]: [
                    this.$t('previewData'),
                    this.$t('viewDetails'),
                    this.$t('placeSchemaInEditor'),
                ],
                [SP]: [this.$t('placeSchemaInEditor')],
                [COL]: [this.$t('placeColumnNameInEditor')],
                [TRIGGER]: [this.$t('placeSchemaInEditor')],
            }
        },
        // more node options for user's nodes
        userNodeOptsMap() {
            const { SCHEMA, TABLE, SP, COL, TRIGGER } = this.SQL_NODE_TYPES
            return {
                [SCHEMA]: [this.$t('dropSchema')],
                [TABLE]: [this.$t('alterTbl'), this.$t('dropTbl')],
                [SP]: [this.$t('dropSp')],
                [COL]: [],
                [TRIGGER]: [this.$t('dropTrigger')],
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
                if (this.getAlteredActiveNode) return [this.getAlteredActiveNode]
                else return [this.getActiveTreeNode]
            },
            set(v) {
                if (v.length) {
                    const activeNodes = this.minimizeNodes(v)
                    if (this.getAlteredActiveNode) {
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
        optionHandler({ item, option }) {
            const schema = item.id
            const txtEditorOptions = [
                this.$t('previewData'),
                this.$t('viewDetails'),
                this.$t('placeSchemaInEditor'),
                this.$t('placeColumnNameInEditor'),
            ]
            if (txtEditorOptions.includes(option))
                this.UPDATE_CURR_EDITOR_MODE_MAP({
                    id: this.active_wke_id,
                    payload: this.SQL_EDITOR_MODES.TXT_EDITOR,
                })

            const prvwDataOpts = [this.$t('previewData'), this.$t('viewDetails')]
            if (prvwDataOpts.includes(option)) {
                /**
                 * If altered_active_node exists, clear it first so that
                 * activeNodes can be updated
                 */
                if (this.getAlteredActiveNode)
                    // Clear altered active node
                    this.UPDATE_TBL_CREATION_INFO_MAP({
                        id: this.active_wke_id,
                        payload: {
                            altered_active_node: null,
                        },
                    })
                this.updateActiveNode(item)
            }
            switch (option) {
                case this.$t('previewData'):
                    this.$emit('preview-data', schema)
                    break
                case this.$t('viewDetails'):
                    this.$emit('view-details', schema)
                    break
                case this.$t('placeSchemaInEditor'):
                    this.$emit('place-to-editor', this.$help.escapeIdentifiers(schema))
                    break
                case this.$t('placeColumnNameInEditor'):
                    this.$emit('place-to-editor', this.$help.escapeIdentifiers(item.name))
                    break
                case this.$t('useDb'):
                    this.$emit('use-db', schema)
                    break
                case this.$t('alterTbl'):
                    {
                        const alterActiveNode = { id: item.id, type: item.type, level: item.level }
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
                    this.$emit('drop-action', { id: schema, type: item.type })
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
            return [...this.baseOptsMap[node.type], ...this.userNodeOptsMap[node.type]]
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
