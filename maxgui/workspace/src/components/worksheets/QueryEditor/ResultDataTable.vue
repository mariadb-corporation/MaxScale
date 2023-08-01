<template>
    <div>
        <div ref="tableTools" class="table-tools pb-2 d-inline-flex align-center">
            <slot name="left-table-tools-prepend" />
            <v-text-field
                v-model="filterKeyword"
                name="filter"
                dense
                outlined
                height="28"
                class="vuetify-input--override filter-result mr-2"
                :placeholder="$mxs_t('filterResult')"
                hide-details
            />
            <mxs-filter-list
                v-model="filterHeaderIdxs"
                selectAllOnCreated
                :label="$mxs_t('filterBy')"
                :items="tableHeaders"
                :maxHeight="tableHeight - 20"
            />
            <slot name="left-table-tools-append" />
            <v-spacer />
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
            <result-export
                :rows="filteredRows_wo_idx"
                :headers="visHeaders_wo_idx"
                :defExportFileName="defExportFileName"
            />
            <mxs-filter-list
                v-model="visHeaderIdxs"
                selectAllOnCreated
                :label="$mxs_t('columns')"
                :items="tableHeaders"
                :maxHeight="tableHeight - 20"
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
        <keep-alive>
            <mxs-virtual-scroll-tbl
                class="pb-2"
                :headers="visibleHeaders"
                :rows="filteredRows"
                :itemHeight="30"
                :maxHeight="tableHeight"
                :boundingWidth="width"
                :isVertTable="isVertTable"
                :showSelect="showSelect"
                :groupBy="groupBy"
                :activeRow="activeRow"
                :search="filterKeyword"
                :selectedItems.sync="selectedItems"
                @is-grouping="isGrouping = $event"
                @on-cell-right-click="onCellRClick"
                v-on="$listeners"
            >
                <template v-for="h in visibleHeaders" v-slot:[h.text]="{ data }">
                    <editable-cell
                        v-if="isEditing && h.editableCol"
                        :key="`${h.text}-${data.cell}`"
                        :cellItem="
                            toCellItem({ rowData: data.rowData, cell: data.cell, colName: h.text })
                        "
                        :changedCells.sync="changedCells"
                    />
                    <slot v-else :name="`${h.text}`" :data="data" />
                </template>
                <template v-for="h in visibleHeaders" v-slot:[`header-${h.text}`]="{ data }">
                    <slot :name="`header-${h.text}`" :data="data" />
                </template>
            </mxs-virtual-scroll-tbl>
        </keep-alive>
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
 * Change Date: 2027-07-24
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/*
@on-delete-selected: selectedItems:any[]. Event is emitted when showSelect props is true
@on-done-editing: changedCells:[].  cells have its value changed
Also emits other events from mxs-virtual-scroll-tbl via v-on="$listeners"
*/
import ResultExport from '@wkeComps/QueryEditor/ResultExport'
import EditableCell from '@wkeComps/QueryEditor/EditableCell'
import { mapState } from 'vuex'
export default {
    name: 'result-data-table',
    components: {
        ResultExport,
        EditableCell,
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
        rows: { type: Array, required: true },
        height: { type: Number, required: true },
        width: { type: Number, required: true },
        showSelect: { type: Boolean, default: false },
        groupBy: { type: String, default: '' },
        showGroupBy: { type: Boolean, default: false },
        //menuOpts:[{ text:string, type:string, action:function}]
        menuOpts: { type: Array, default: () => [] },
        showEditBtn: { type: Boolean, default: false },
        defExportFileName: { type: String, default: 'MaxScale Query Results' },
    },
    data() {
        return {
            filterHeaderIdxs: [],
            visHeaderIdxs: [],
            filterKeyword: '',
            tableToolsHeight: 0,
            isVertTable: false,
            isGrouping: false,
            selectedItems: [],
            // states for ctx menu
            showCtxMenu: false,
            ctxMenuData: {},
            // states for editing table cell
            isEditing: false,
            changedCells: [], // cells have its value changed
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
        tableHeaders() {
            let headers = []
            if (this.headers.length)
                headers = [
                    { text: '#', maxWidth: 'max-content' },
                    ...this.headers.map(h =>
                        this.showGroupBy && !this.$typy(h, 'groupable').isDefined
                            ? { ...h, groupable: true, draggable: this.draggable }
                            : { ...h, draggable: this.draggable }
                    ),
                ]

            return headers
        },
        rowsWithIndex() {
            return this.rows.map((row, i) => [i + 1, ...row])
        },
        filteredRows_wo_idx() {
            return this.filteredRows.map(row => row.filter((cell, i) => i !== 0))
        },
        visHeaders_wo_idx() {
            return this.visibleHeaders.filter(header => header.text !== '#')
        },
        filteredRows() {
            return this.rowsWithIndex.filter(row => {
                let match = false
                for (const [i, cell] of row.entries()) {
                    if (
                        (this.filterHeaderIdxs.includes(i) || !this.filterHeaderIdxs.length) &&
                        this.handleFilter(cell, this.filterKeyword, i)
                    ) {
                        match = true
                        break
                    }
                }
                return match
            })
        },
        visibleHeaders() {
            return this.tableHeaders.map((h, i) =>
                this.visHeaderIdxs.includes(i) ? h : { ...h, hidden: true }
            )
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
            return [
                {
                    text: this.$mxs_t('placeToEditor'),
                    children: this.insertOpts,
                },
                {
                    text: this.$mxs_t('copyToClipboard'),
                    children: this.clipboardOpts,
                },
            ]
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
    },
    activated() {
        this.setTableToolsHeight()
    },
    methods: {
        setTableToolsHeight() {
            if (!this.$refs.tableTools) return
            this.tableToolsHeight = this.$refs.tableTools.clientHeight
        },
        handleFilter(value, search, cellIdx) {
            const header = this.$typy(this.tableHeaders[cellIdx]).safeObjectOrEmpty
            // use custom filter if it's provided
            if (header.filter) return header.filter(value, search)
            return this.$helpers.ciStrIncludes(`${value}`, search)
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
