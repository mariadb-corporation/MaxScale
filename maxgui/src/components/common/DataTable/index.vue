<template>
    <div>
        <v-data-table
            v-sortable-table
            :headers="headers"
            :items="!loading ? tableRows : []"
            :hide-default-header="true"
            :hide-default-footer="showAll ? true : tableRows.length <= 10"
            :items-per-page="showAll ? -1 : itemsPerPage"
            :class="['data-table-full', tableClass]"
            :loading="loading"
            :options.sync="pagination"
            :page="page"
            :sort-by="sortBy"
            :sort-desc="sortDesc"
            :search="search"
            item-key="id"
            :dense="dense"
            :no-data-text="noDataText"
            :custom-sort="customSort"
            @current-items="getCurrentItems"
            @on-drag-end="$emit('on-drag-end', $event)"
        >
            <!----------------------------------------------------TABLE HEAD------------------------------------------>
            <template v-slot:header="{ props: { headers } }">
                <table-header
                    :headers="headers"
                    :sortDesc="pagination.sortDesc.length ? pagination.sortDesc[0] : false"
                    :sortBy="pagination.sortBy.length ? pagination.sortBy[0] : ''"
                    :isTree="isTree"
                    :hasValidChild="hasValidChild"
                    @change-sort="changeSort"
                >
                    <template v-for="header in headers" :slot="`header-append-${header.value}`">
                        <slot :name="`header-append-${header.value}`"></slot>
                    </template>
                </table-header>
            </template>

            <!----------------------------------------------------TABLE ROW------------------------------------------->
            <template v-slot:item="{ item, index: rowIndex }">
                <table-row
                    :key="item.nodeId || item.id"
                    :rowIndex="rowIndex"
                    :editableCell="editableCell"
                    :draggable="draggable"
                    :showActionsOnHover="showActionsOnHover"
                    :lastPageItemIndex="lastPageItemIndex"
                >
                    <template v-slot:cell="{ data: { indexOfHoveredRow } }">
                        <table-cell
                            v-for="(header, cellIndex) in headers"
                            :key="cellIndex"
                            :ref="
                                cellIndex < colsHasRowSpan
                                    ? `${item.groupId}RowspanCell`
                                    : `${item.groupId}Cell`
                            "
                            :cellIndex="cellIndex"
                            :colsHasRowSpan="colsHasRowSpan"
                            :item="item"
                            :header="header"
                            :indexOfLastColumn="headers.length - 1"
                            :rowIndex="rowIndex"
                            :hasOrderNumber="hasOrderNumber"
                            :editableCell="editableCell"
                            :tdBorderLeft="tdBorderLeft"
                            :draggable="draggable"
                            :indexOfHoveredRow="indexOfHoveredRow"
                            :isTree="isTree"
                            :hasValidChild="hasValidChild"
                            :componentId="componentId"
                            @cell-hover="cellHover($event)"
                            @get-truncated-info="truncatedMenu = $event"
                            @toggle-node="toggleNode($event)"
                        >
                            <template :slot="header.value">
                                <slot
                                    :name="header.value"
                                    :data="{ item, header, cellIndex, rowIndex }"
                                >
                                    {{ getValue(item, header) }}
                                </slot>
                            </template>
                            <template slot="actions">
                                <slot name="actions" :data="{ item }" />
                            </template>
                        </table-cell>
                    </template>
                </table-row>
            </template>
        </v-data-table>
        <v-menu
            offset-x
            transition="slide-x-transition"
            :close-on-content-click="false"
            open-on-hover
            :nudge-left="truncatedMenu.x"
            :nudge-top="truncatedMenu.y"
            content-class="shadow-drop color text-navigation"
            :activator="
                `#truncatedText_atRow${truncatedMenu.rowIndex}_atCell${truncatedMenu.cellIndex}_${componentId}`
            "
        >
            <v-sheet
                v-if="truncatedMenu.item && truncatedMenu.index === truncatedMenu.cellIndex"
                style="border-radius: 10px;overflow:auto"
                class="pa-4"
            >
                <span class="body-2">
                    {{ getValue(truncatedMenu.item, truncatedMenu.header) }}
                </span>
            </v-sheet>
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
 * Change Date: 2024-06-15
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/*
This component accepts:
- headers props as array of objects, each object must has text:String, value:Any and
  may has sortable: true || false, editableCol: true || false, align: "center || left || right",
  cellTruncated: true || false, width: String, padding: String

- data props as array of objects, each object must has either item.nodeId:Number || item.id:Any, 
  if both presents nodeId will be used

SLOTS available for this component:
- slot :name="header.value" // slot aka item
- slot  name="actions" :data="{ item }"
- slot :name="`header-append-${header.value}`"

Emits:
- $emit('on-drag-end', event:Object)
- $emit('pagination', val:Object)
- $emit('cell-hover', { e, item, rowIndex, cellIndex, header })
*/
import Sortable from 'sortablejs'
import TableHeader from './TableHeader'
import TableCell from './TableCell'
import TableRow from './TableRow'

export default {
    name: 'data-table',
    components: {
        TableHeader,
        TableRow,
        TableCell,
    },
    directives: {
        sortableTable: {
            bind(el, binding, vnode) {
                const options = {
                    handle: '.drag-handle',
                    draggable: '.draggable-row',
                    animation: 200,
                    onEnd: function(event) {
                        vnode.child.$emit('on-drag-end', event)
                    },
                }
                Sortable.create(el.getElementsByTagName('tbody')[0], options)
            },
        },
    },
    props: {
        headers: { type: Array, required: true },
        data: { type: Array, required: true },
        sortBy: { type: String },
        search: { type: String, default: '' },
        sortDesc: { type: Boolean },
        loading: { type: Boolean, default: false },
        tableClass: { type: String },
        itemsPerPage: { type: Number, default: 10 },
        showAll: { type: Boolean, default: false },
        page: { type: Number, default: 1 },
        dense: { type: Boolean, default: false },
        noDataText: { type: String },
        // add border left to td
        tdBorderLeft: { type: Boolean, default: false },
        // For editable feature
        editableCell: { type: Boolean, default: false },
        // For table wants to keep primitive value, eg:if it is false, null/undefined won't be displayed
        keepPrimitiveValue: { type: Boolean, default: false },
        // For draggable feature
        draggable: { type: Boolean, default: false },
        /*
        enable hasOrderNumber to display item index column, however,
        item needs to have its own index property because using table row index will not work properly
        when table items is being searched
         */
        hasOrderNumber: { type: Boolean, default: false },
        showActionsOnHover: { type: Boolean, default: false },
        // rowspan feature, data array must contains objects having groupId property.
        colsHasRowSpan: { type: Number },
        // if data has child object or array, enable this props in adanvce
        isTree: { type: Boolean, default: false },
    },
    data() {
        return {
            //common
            pagination: {},

            //For truncated cell
            truncatedMenu: { index: null, x: 0, y: 16.5 },
            //For nested data, display dropdown table row
            hasValidChild: false,
            nodeActiveIds: [],
            //rowspan feature
            lastPageItemIndex: null,
            // this is needed when using custom activator in v-menu.
            componentId: this.$help.lodash.uniqueId('component_v-menu_'),
        }
    },
    computed: {
        // first processing data from data props to whether keepPrimitiveValue or not
        processingData: function() {
            const self = this
            let result = self.data
            if (!self.keepPrimitiveValue) {
                result = self.$help.lodash.cloneDeep(self.data)
                for (let i = 0; i < result.length; ++i) {
                    let obj = result[i]
                    Object.keys(obj).forEach(key => (obj[key] = self.$help.handleValue(obj[key])))
                }
            }

            return result
        },

        tableRows: function() {
            const self = this
            if (self.isTree) {
                let newArr = []
                self.levelRecursive(self.processingData, newArr, self.nodeActiveIds)
                return newArr
            } else return self.processingData
        },
    },

    watch: {
        pagination: {
            handler(val) {
                this.$emit('pagination', val)
            },
            deep: true,
        },

        editableCell: function(val) {
            if (val && this.isTree && this.hasValidChild) {
                this.expandAllNodes(this.tableRows)
            }
        },
    },

    methods: {
        //---------------------------------Table events----------------------------------------------------------------
        cellHover({ e, item, rowIndex, cellIndex, header }) {
            this.$emit('cell-hover', {
                e,
                item,
                rowIndex,
                cellIndex,
                header,
            })

            if (this.colsHasRowSpan) {
                this.setRowspanBg(e, item, rowIndex, cellIndex)
            }
        },

        //---------------------------------Table helper functions ------------------------------------------
        getValue(item, header) {
            // data type shouldn't be handled here as it will break the filter result
            // use helper function to handle value before passing the data to table
            let value = item[header.value]
            return this.$help.isFunction(header.format) ? header.format(value) : `${value}`
        },

        //--------------------------------- @private Table sorting ---------------------------------------------------
        // Currently support sorting one column at a time
        customSort(items, sortBy, isDesc) {
            let result = items
            const self = this

            // if isTree, create a hash array for hierarchySort
            if (sortBy.length && this.isTree) {
                let hashArr = {} // O(n log n)

                for (let i = 0; i < items.length; ++i) {
                    if (hashArr[items[i].parentNodeId] == undefined)
                        hashArr[items[i].parentNodeId] = []
                    hashArr[items[i].parentNodeId].push(items[i])
                }

                result = this.hierarchySort(hashArr, 0, sortBy, isDesc, [])
            } else if (sortBy.length && !this.isTree) {
                result = items.sort((a, b) => self.sortOrder(a, b, isDesc, sortBy))
            }

            // if rowspan feature is enabled, processing sorted arr
            if (this.colsHasRowSpan && result.length) {
                const newArr = this.processingRowspanTable(result)
                result = newArr
            }

            return result
        },

        hierarchySort(hashArr, key, sortBy, isDesc, result) {
            if (hashArr[key] === undefined) return result
            const self = this
            let arr = hashArr[key].sort((a, b) => self.sortOrder(a, b, isDesc, sortBy))
            for (let i = 0; i < arr.length; ++i) {
                result.push(arr[i])
                const key = arr[i].nodeId || arr[i].id
                self.hierarchySort(hashArr, key, sortBy, isDesc, result)
            }
            return result
        },

        sortOrder(a, b, isDesc, sortBy) {
            if (isDesc[0]) {
                return b[sortBy] < a[sortBy] ? -1 : 1
            } else {
                return a[sortBy] < b[sortBy] ? -1 : 1
            }
        },

        changeSort(column) {
            // TODO: support multiple column sorting
            if (this.pagination.sortBy[0] === column) {
                this.pagination.sortDesc = [!this.pagination.sortDesc[0]]
            } else {
                this.pagination.sortBy = [column]
                this.pagination.sortDesc = [false]
            }
        },

        //--------------------------------- @private methods for Rowspan feature----------------------------------------
        getCurrentItems(items) {
            // This ensure rowspan table feature works
            this.colsHasRowSpan && items.length && this.processingRowspanTable(items, 'mutate')
            // for styling purpose
            this.lastPageItemIndex = items.length - 1
        },

        /**
         * @param {Array} data Array of objects
         * @param {String} mode mutate or undefined
         * @return {Array} Return new array if mode!=='mutate'
         * if mode ==='mutate', changes will be mutated, otherwise it returns a new array
         */
        processingRowspanTable(data, mode) {
            if (mode === 'mutate') this.handleDisplayRowspan(data)
            else return this.handleDisplayRowspan(this.$help.lodash.cloneDeep(data))
        },

        /**
         * @param {Array} target Array of objects,
         * @return {Array} Always return new array
         * This function group all items have same groupdID and assign correct value for hidden and rowspan properties.
         */
        handleDisplayRowspan(target) {
            let uniqueSet = new Set(target.map(item => item.groupId))
            let itemsId = [...uniqueSet]
            let groupedId = this.$help.groupBy(target, 'groupId')
            let result = []
            for (let i = 0; i < itemsId.length; ++i) {
                let group = groupedId[`${itemsId[i]}`]

                for (let n = 0; n < group.length; ++n) {
                    group[n].rowspan = group.length
                    if (n === 0) group[n].hidden = false
                    else group[n].hidden = true
                    result.push(group[n])
                }
            }
            return result
        },

        /**
         * @param {Object} e event object
         * @param {Object} item object
         * This function set background color to rows when a cell or a row is hovered
         * This function is used when rowspan feature is enabled
         */
        setRowspanBg(e, item, rowIndex, cellIndex) {
            const target = cellIndex < this.colsHasRowSpan ? 'rowspanCell' : 'cell'
            const { groupId } = item
            // Make associated td elements to have the same hover effect
            let bg = e.type === 'mouseenter' ? '#fafcfc' : ''
            switch (target) {
                case 'cell':
                    {
                        let cellComponents = this.$refs[`${groupId}RowspanCell`]
                        cellComponents.forEach(ele => (ele.$el.style.backgroundColor = bg))
                    }
                    break
                case 'rowspanCell':
                    {
                        let cellComponents = this.$refs[`${groupId}Cell`]
                        cellComponents.forEach(ele => (ele.$el.style.backgroundColor = bg))
                    }
                    break
            }
        },

        //--------------------------------- @private methods: For nested data, displaying dropdown table row-----------
        /**
         * @param {Array} arr root array
         * @param {Array} newArr result array
         * @param {Array} nodeActiveIds array of active node ( node has been opened)
         * @return {Array} newArr new array that has element of root array and its children
         */
        levelRecursive(arr, newArr, nodeActiveIds) {
            let self = this
            arr.forEach(function(o) {
                if (o.children && o.children.length > 0) {
                    !self.hasValidChild && (self.hasValidChild = true)
                    newArr.push(o)
                    for (let i = 0; i < nodeActiveIds.length; ++i) {
                        if (o.nodeId === nodeActiveIds[i]) {
                            o.expanded = true
                        }
                    }
                    if (o.expanded === true) {
                        self.levelRecursive(o.children, newArr, nodeActiveIds)
                    }
                } else {
                    newArr.push(o)
                }
            })
        },

        toggleNode(node) {
            const self = this
            // expand node's children
            if (node.leaf === false && node.expanded === false && node.children.length > 0) {
                self.nodeActiveIds.push(node.nodeId)
                self.levelRecursive(node.children, [], self.nodeActiveIds, true)
            } else {
                // collapse node's children
                const isExpand = false
                this.toggleNodeChildren(node, isExpand)
            }
        },

        /**
         * @param {Object} node an object node
         * @param {Boolean} isExpand if it is true, it will expand the node
         * otherwise it collapse the node.
         */
        toggleNodeChildren(node, isExpand) {
            const self = this
            if (node.expanded === !isExpand && node.children.length > 0) {
                self.$set(node, 'expanded', isExpand)
                node.children.forEach(o => {
                    self.toggleNodeChildren(o, isExpand)
                })
                isExpand
                    ? self.nodeActiveIds.push(node.nodeId)
                    : self.nodeActiveIds.splice(self.nodeActiveIds.indexOf(node.nodeId), 1)
            }
        },

        /**
         * @param {Array} treeNodes treeNodes array processed by objToArrOfObj helper method
         */
        expandAllNodes(treeNodes) {
            const isExpand = true
            for (let i = 0; i < treeNodes.length; ++i) {
                let node = treeNodes[i]
                this.toggleNodeChildren(node, isExpand)
            }
        },
    },
}
</script>

<style lang="scss" scoped>
.draggable-row:hover {
    background: transparent !important;
}

.sortable-chosen:hover {
    background: #f2fcff !important;
    .drag-handle {
        display: inline;
    }
}
.sortable-ghost {
    background: #f2fcff !important;
    opacity: 0.6;
}
</style>
