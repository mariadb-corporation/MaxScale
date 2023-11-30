<template>
    <div>
        <div ref="tableTools" class="table-tools pb-2 d-inline-flex align-center">
            <slot name="left-table-tools-prepend" />
            <mxs-debounced-field
                v-model="search"
                name="filter"
                dense
                outlined
                height="28"
                class="vuetify-input--override filter-result mr-2"
                :placeholder="$mxs_t('filterResult')"
                hide-details
            />
            <mxs-filter-list
                v-model="excludedSearchHeaderIndexes"
                :label="$mxs_t('filterBy')"
                :items="visHeaderNames"
                :maxHeight="tableHeight - 20"
                returnIndex
                activatorClass="mr-2"
            />
            <group-by
                v-model="activeGroupByColIdx"
                :items="visHeaderNames"
                :maxHeight="tableHeight - 20"
                :disabled="disableGrouping"
            />
            <slot name="left-table-tools-append" />
            <v-spacer />
            <v-tooltip v-if="columnsLimitInfo" top transition="slide-y-transition" max-width="400">
                <template v-slot:activator="{ on }">
                    <span class="text-truncate mx-2 d-flex align-center" v-on="on">
                        <v-icon size="16" color="warning" class="mr-2">
                            $vuetify.icons.mxs_alertWarning
                        </v-icon>
                        {{ $mxs_t('columnsLimit') }}
                    </span>
                </template>
                {{ columnsLimitInfo }}
            </v-tooltip>
            <slot name="right-table-tools-prepend" />
            <v-btn
                v-if="showEditBtn"
                x-small
                class="mr-2 pa-1 text-capitalize font-weight-medium"
                outlined
                depressed
                color="primary"
                @click="handleEdit"
            >
                {{ isEditing ? $mxs_t('doneEditing') : $mxs_t('edit') }}
            </v-btn>
            <mxs-tooltip-btn
                v-if="selectedItems.length"
                btnClass="mr-2 pa-1 text-capitalize font-weight-medium"
                x-small
                outlined
                depressed
                color="error"
                @click="$emit('on-delete-selected', selectedItems)"
            >
                <template v-slot:btn-content>
                    {{ $mxs_t('delete') }} ({{ selectedItems.length }})
                </template>
                {{ $mxs_t('deleteSelectedRows') }}
            </mxs-tooltip-btn>
            <result-export :rows="data" :fields="fields" :defExportFileName="defExportFileName" />
            <mxs-filter-list
                v-model="hiddenHeaderIndexes"
                :label="$mxs_t('columns')"
                :items="allHeaderNames"
                :maxHeight="tableHeight - 20"
                returnIndex
            />
            <mxs-tooltip-btn
                btnClass="ml-2 pa-1"
                x-small
                outlined
                depressed
                color="primary"
                :disabled="isGrouping"
                @click="isVertTable = !isVertTable"
            >
                <template v-slot:btn-content>
                    <v-icon size="14" :class="{ 'rotate-left': !isVertTable }">
                        mdi-format-rotate-90
                    </v-icon>
                </template>
                {{ $mxs_t(isVertTable ? 'switchToHorizTable' : 'switchToVertTable') }}
            </mxs-tooltip-btn>
            <slot name="right-table-tools-append" />
        </div>
        <mxs-virtual-scroll-tbl
            class="pb-2"
            :headers="tableHeaders"
            :data="tableData"
            :itemHeight="30"
            :maxHeight="tableHeight"
            :boundingWidth="width"
            :isVertTable="isVertTable"
            :showSelect="showSelect"
            :groupByColIdx.sync="activeGroupByColIdx"
            :activeRow="activeRow"
            :search="search"
            :filterByColIndexes="filterByColIndexes"
            :selectedItems.sync="selectedItems"
            @on-cell-right-click="onCellRClick"
            @current-rows="currentRows = $event"
            v-on="$listeners"
        >
            <template v-for="h in tableHeaders" v-slot:[h.text]="props">
                <editable-cell
                    v-if="isEditing && h.editableCol"
                    :key="`${h.text}-${props.data.cell}`"
                    :cellItem="
                        toCellItem({
                            rowData: props.data.rowData,
                            cell: props.data.cell,
                            colName: h.text,
                        })
                    "
                    :changedCells.sync="changedCells"
                />
                <slot v-else :name="`${h.text}`" v-bind="props" />
            </template>
            <template v-for="h in tableHeaders" v-slot:[`header-${h.text}`]="{ data }">
                <slot :name="`header-${h.text}`" :data="data" />
            </template>
        </mxs-virtual-scroll-tbl>
        <mxs-sub-menu
            v-if="!$typy(ctxMenuData).isEmptyObject"
            :key="ctxMenuActivator"
            v-model="showCtxMenu"
            left
            offset-y
            transition="slide-y-transition"
            :items="menuItems"
            :activator="ctxMenuActivator"
            @item-click="onChooseOpt"
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
@on-delete-selected: selectedRowIndexes:number[]. Event is emitted when showSelect props is true
@on-done-editing: changedCells:[].  cells have its value changed
Also emits other events from mxs-virtual-scroll-tbl via v-on="$listeners"
*/
import ResultExport from '@wkeComps/QueryEditor/ResultExport'
import EditableCell from '@wkeComps/QueryEditor/EditableCell'
import GroupBy from '@wkeComps/QueryEditor/GroupBy'
import { mapState } from 'vuex'
export default {
    name: 'result-data-table',
    components: {
        ResultExport,
        EditableCell,
        GroupBy,
    },
    props: {
        headers: {
            type: Array,
            validator: arr => {
                if (!arr.length) return true
                else return arr.filter(item => 'text' in item).length === arr.length
            },
            required: true,
        },
        data: { type: Array, required: true },
        height: { type: Number, required: true },
        width: { type: Number, required: true },
        showSelect: { type: Boolean, default: false },
        groupByColIdx: { type: Number, default: -1 },
        showGroupBy: { type: Boolean, default: false },
        //menuOpts:[{ text:string, type:string, action:function}]
        menuOpts: { type: Array, default: () => [] },
        showEditBtn: { type: Boolean, default: false },
        defExportFileName: { type: String, default: 'MaxScale Query Results' },
        hasInsertOpt: { type: Boolean, default: true },
    },
    data() {
        return {
            excludedSearchHeaderIndexes: [],
            hiddenHeaderIndexes: [],
            currentRows: [],
            search: '',
            tableToolsHeight: 0,
            isVertTable: false,
            activeGroupByColIdx: -1,
            selectedItems: [],
            // states for ctx menu
            showCtxMenu: false,
            ctxMenuData: {},
            // states for editing table cell
            isEditing: false,
            changedCells: [], // cells have its value changed
            columnsLimitInfo: '',
        }
    },
    computed: {
        ...mapState({
            NODE_CTX_TYPES: state => state.mxsWorkspace.config.NODE_CTX_TYPES,
        }),
        tableHeight() {
            return this.height - this.tableToolsHeight - 8
        },
        draggable() {
            return !this.isEditing
        },
        headersLength() {
            return this.headers.length
        },
        tableHeaders() {
            let headers = []
            if (this.headersLength)
                headers = [
                    {
                        text: '#',
                        maxWidth: 'max-content',
                        hidden: this.hiddenHeaderIndexes.includes(0),
                    }, // order number col
                    ...this.headers.map((h, i) => ({
                        ...h,
                        resizable: true,
                        draggable: this.draggable,
                        hidden: this.hiddenHeaderIndexes.includes(i + 1),
                        useCellSlot: h.useCellSlot || (this.isEditing && h.editableCol),
                    })),
                ]

            return headers
        },
        allHeaderNames() {
            return this.tableHeaders.map(h => h.text)
        },
        visibleHeaders() {
            return this.tableHeaders.filter(h => !h.hidden)
        },
        visHeaderNames() {
            return this.visibleHeaders.map(h => h.text)
        },
        filterByColIndexes() {
            return this.allHeaderNames.reduce((acc, _, index) => {
                if (!this.excludedSearchHeaderIndexes.includes(index)) acc.push(index)
                return acc
            }, [])
        },
        disableGrouping() {
            return this.visHeaderNames.length <= 1 || this.isVertTable
        },
        isGrouping() {
            return this.activeGroupByColIdx >= 0
        },
        tableData() {
            return this.data.map((row, i) => [i + 1, ...row]) // add order number cell
        },
        fields() {
            return this.headers.map(h => h.text)
        },
        activeRow() {
            return this.$typy(this.ctxMenuData, 'row').safeArray
        },
        ctxMenuActivator() {
            return `#${this.$typy(this.ctxMenuData, 'activatorID').safeString}`
        },
        clipboardOpts() {
            const { CLIPBOARD } = this.NODE_CTX_TYPES
            return this.genTxtOpts(CLIPBOARD)
        },
        insertOpts() {
            const { INSERT } = this.NODE_CTX_TYPES
            return this.genTxtOpts(INSERT)
        },
        baseOpts() {
            let opts = [{ text: this.$mxs_t('copyToClipboard'), children: this.clipboardOpts }]
            if (this.hasInsertOpt)
                opts.unshift({
                    text: this.$mxs_t('placeToEditor'),
                    children: this.insertOpts,
                })
            return opts
        },
        menuItems() {
            if (this.menuOpts.length) {
                // Deep merge of menuOpts with baseOpts
                const { mergeWith, keyBy, values } = this.$helpers.lodash
                const merged = values(
                    mergeWith(
                        keyBy(this.baseOpts, 'text'),
                        keyBy(this.menuOpts, 'text'),
                        (objVal, srcVal) => {
                            if (Array.isArray(objVal)) {
                                return objVal.concat(srcVal)
                            }
                        }
                    )
                )
                return merged
            }
            return this.baseOpts
        },
    },
    watch: {
        showCtxMenu(v) {
            // when menu is closed by blur event, clear ctxMenuData so that activeRow can be reset
            if (!v) this.ctxMenuData = {}
        },
        headersLength: {
            immediate: true,
            handler(v) {
                if (v > 50) {
                    this.hiddenHeaderIndexes = Array.from(
                        { length: this.tableHeaders.length - 50 },
                        (_, index) => index + 50
                    )
                    this.columnsLimitInfo = this.$mxs_t('info.columnsLimit')
                }
            },
        },
    },
    created() {
        this.activeGroupByColIdx = this.groupByColIdx
    },
    mounted() {
        this.setTableToolsHeight()
    },
    methods: {
        setTableToolsHeight() {
            if (!this.$refs.tableTools) return
            this.tableToolsHeight = this.$refs.tableTools.clientHeight
        },
        /**
         * @param {Object} data { e: event, row:[], cell:string, activatorID:string }
         */
        onCellRClick(data) {
            const { activatorID } = data
            if (this.$typy(this.ctxMenuData, 'activatorID').safeString === activatorID) {
                this.showCtxMenu = false
                this.ctxMenuData = {}
            } else {
                this.showCtxMenu = true
                this.ctxMenuData = data
            }
        },
        /**
         * Both INSERT and CLIPBOARD types have same options & action
         * This generates txt options based on provided type
         * @param {String} type - INSERT OR CLIPBOARD
         * @returns {Array} - return context options
         */
        genTxtOpts(type) {
            return [this.$mxs_t('fieldQuoted'), this.$mxs_t('field')].map(text => ({
                text,
                action: ({ opt, data }) => this.handleTxtOpt({ opt, data }),
                type,
            }))
        },
        // Handle edge case when cell value is an object. e.g. In History table
        processField(cell) {
            // convert to string with template literals
            return this.$typy(cell).isObject ? `${cell.name}` : `${cell}`
        },
        /**
         * Both INSERT and CLIPBOARD types have same options and action
         * This handles INSERT and CLIPBOARD options
         * @param {data} item - data
         * @param {Object} opt - context menu option
         */
        handleTxtOpt({ opt, data }) {
            const { CLIPBOARD, INSERT } = this.NODE_CTX_TYPES
            let v = ''
            switch (opt.text) {
                case this.$mxs_t('fieldQuoted'):
                    v = this.$helpers.quotingIdentifier(this.processField(data.cell))
                    break
                case this.$mxs_t('field'):
                    v = this.processField(data.cell)
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
        onChooseOpt(opt) {
            // pass arguments opt and data to action function
            opt.action({ opt, data: this.ctxMenuData })
        },
        handleEdit() {
            this.isEditing = !this.isEditing
            if (!this.isEditing) this.$emit('on-done-editing', this.changedCells)
        },
        toCellItem({ rowData, cell, colName }) {
            const objRow = this.tableHeaders.reduce((o, c, i) => ((o[c.text] = rowData[i]), o), {})
            const rowId = objRow['#'] // Using # col as unique row id as its value isn't alterable
            const cellItem = {
                id: `rowId-${rowId}_colName-${colName}`,
                rowId,
                colName,
                value: cell,
                objRow,
            }
            return cellItem
        },
    },
}
</script>

<style lang="scss" scoped>
.vuetify-input--override.filter-result {
    max-width: 250px;
}
.table-tools {
    width: 100%;
}
</style>
