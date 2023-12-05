<template>
    <mxs-virtual-scroll-tbl
        :key="keyId"
        :headers="headers"
        :data="rows"
        :itemHeight="32"
        :maxHeight="dim.height"
        :boundingWidth="dim.width"
        showSelect
        :style="{ width: `${dim.width}px` }"
        :selectedItems.sync="selectedItems"
        :showRowCount="false"
        @row-click="onRowClick"
    >
        <template v-slot:[KEY_COL_EDITOR_ATTRS.COL_ORDER]="{ data: { rowData } }">
            {{ getColOrder(rowData) }}
        </template>
        <template
            v-slot:[KEY_COL_EDITOR_ATTRS.ORDER_BY]="{ data: { cell, rowIdx, colIdx, rowData } }"
        >
            <lazy-select
                :value="cell"
                :name="KEY_COL_EDITOR_ATTRS.ORDER_BY"
                :height="28"
                :items="orderByItems"
                @on-input="onChangeInput({ rowIdx, rowData, colIdx, value: $event })"
            />
        </template>
        <template
            v-slot:[KEY_COL_EDITOR_ATTRS.LENGTH]="{ data: { cell, rowIdx, colIdx, rowData } }"
        >
            <lazy-text-field
                :value="cell"
                :name="KEY_COL_EDITOR_ATTRS.LENGTH"
                :height="28"
                @keypress="$helpers.preventNonNumericalVal($event)"
                @on-input="onChangeInput({ rowIdx, rowData, colIdx, value: $event })"
            />
        </template>
    </mxs-virtual-scroll-tbl>
</template>

<script>
/*
 * Copyright (c) 2023 MariaDB plc
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
import { mapState } from 'vuex'
import LazyTextField from '@wsSrc/components/common/MxsDdlEditor/LazyTextField'
import LazySelect from '@wsSrc/components/common/MxsDdlEditor/LazySelect'
import erdHelper from '@wsSrc/utils/erdHelper'

export default {
    name: 'index-col-list',
    components: { LazyTextField, LazySelect },
    props: {
        value: { type: Object, required: true },
        dim: { type: Object, required: true },
        keyId: { type: String, required: true },
        category: { type: String, required: true },
        tableColNameMap: { type: Object, required: true },
        tableColMap: { type: Object, required: true },
    },
    data() {
        return {
            stagingCategoryMap: {},
            selectedItems: [],
            rows: [],
        }
    },
    computed: {
        ...mapState({
            KEY_COL_EDITOR_ATTRS: state => state.mxsWorkspace.config.KEY_COL_EDITOR_ATTRS,
            KEY_COL_EDITOR_ATTRS_IDX_MAP: state =>
                state.mxsWorkspace.config.KEY_COL_EDITOR_ATTRS_IDX_MAP,
            COL_ORDER_BY: state => state.mxsWorkspace.config.COL_ORDER_BY,
        }),
        idxOfId() {
            return this.KEY_COL_EDITOR_ATTRS_IDX_MAP[this.KEY_COL_EDITOR_ATTRS.ID]
        },
        idxOfOrderBy() {
            return this.KEY_COL_EDITOR_ATTRS_IDX_MAP[this.KEY_COL_EDITOR_ATTRS.ORDER_BY]
        },
        idxOfLength() {
            return this.KEY_COL_EDITOR_ATTRS_IDX_MAP[this.KEY_COL_EDITOR_ATTRS.LENGTH]
        },
        headers() {
            const { ID, NAME, TYPE, COL_ORDER, ORDER_BY, LENGTH } = this.KEY_COL_EDITOR_ATTRS
            let header = { sortable: false, uppercase: true, useCellSlot: true }
            return [
                { text: ID, hidden: true },
                { text: COL_ORDER, width: 50, minWidth: 50, ...header },
                { text: NAME, minWidth: 90, ...header },
                { text: TYPE, minWidth: 90, ...header },
                { text: ORDER_BY, minWidth: 120, maxWidth: 120, ...header },
                { text: LENGTH, minWidth: 90, maxWidth: 90, ...header },
            ]
        },
        indexedCols() {
            return this.$typy(this.stagingCategoryMap[this.category], `${this.keyId}.cols`)
                .safeArray
        },
        indexedColMap() {
            return this.indexedCols.reduce((map, key, i) => {
                map[key.id] = { ...key, index: i }
                return map
            }, {})
        },
        keyCategoryMap: {
            get() {
                return this.value
            },
            set(v) {
                this.$emit('input', v)
            },
        },
        rowMap() {
            return this.$helpers.lodash.keyBy(this.rows, row => row[this.idxOfId])
        },
        orderByItems() {
            return Object.values(this.COL_ORDER_BY)
        },
    },
    watch: {
        // initialize with fresh data
        keyCategoryMap: {
            deep: true,
            handler(v) {
                if (!this.$helpers.lodash.isEqual(v, this.stagingCategoryMap)) this.init()
            },
        },
        keyId(v, oV) {
            if (v !== oV) this.init()
        },
        // sync changes to keyCategoryMap
        stagingCategoryMap: {
            deep: true,
            handler(v) {
                if (!this.$helpers.lodash.isEqual(this.keyCategoryMap, v)) this.keyCategoryMap = v
            },
        },
        selectedItems: {
            deep: true,
            handler() {
                this.syncKeyCols()
            },
        },
    },
    created() {
        this.init()
    },
    methods: {
        init() {
            this.stagingCategoryMap = this.$helpers.lodash.cloneDeep(this.keyCategoryMap)
            this.setRows()
            this.setInitialSelectedItems()
        },
        setRows() {
            this.rows = erdHelper
                .genIdxColOpts({ tableColMap: this.tableColMap })
                .map(({ id, text, type }) => {
                    const indexedCol = this.indexedColMap[id]
                    const order =
                        this.$typy(indexedCol, 'order').safeString || this.COL_ORDER_BY.ASC
                    const length = this.$typy(indexedCol, 'length').safeString || undefined
                    return [id, '', text, type, order, length]
                })
        },
        setInitialSelectedItems() {
            const ids = Object.keys(this.indexedColMap)
            this.selectedItems = ids.map(id => this.rowMap[id])
        },
        getColOrder(rowData) {
            const id = rowData[this.idxOfId]
            const indexedCol = this.indexedColMap[id]
            return indexedCol ? indexedCol.index : ''
        },
        onChangeInput({ rowIdx, rowData, colIdx, value }) {
            // Update component state
            this.rows = this.$helpers.immutableUpdate(this.rows, {
                [rowIdx]: { [colIdx]: { $set: value } },
            })
            const colId = rowData[this.idxOfId]
            const indexedCol = this.indexedColMap[colId]
            // Update selectedItems
            if (indexedCol) {
                const selectedColIdx = this.selectedItems.findIndex(
                    item => item[this.idxOfId] === colId
                )
                if (selectedColIdx >= 0)
                    this.selectedItems = this.$helpers.immutableUpdate(this.selectedItems, {
                        [selectedColIdx]: { [colIdx]: { $set: value } },
                    })
            }
        },
        onRowClick(rowData) {
            const index = this.selectedItems.findIndex(
                item => item[this.idxOfId] === rowData[this.idxOfId]
            )
            if (index >= 0) this.selectedItems.splice(index, 1)
            else this.selectedItems.push(rowData)
        },

        /**
         * Keeps selectedItems them in sync with indexedCols
         */
        syncKeyCols() {
            const cols = this.selectedItems.reduce((acc, item) => {
                const id = item[this.idxOfId]
                const order = item[this.idxOfOrderBy]
                const length = item[this.idxOfLength]
                let col = { id }
                if (order !== this.COL_ORDER_BY.ASC) col.order = order
                if (length > 0) col.length = length
                acc.push(col)
                return acc
            }, [])

            this.stagingCategoryMap = this.$helpers.immutableUpdate(this.stagingCategoryMap, {
                [this.category]: { [this.keyId]: { cols: { $set: cols } } },
            })
        },
    },
}
</script>
