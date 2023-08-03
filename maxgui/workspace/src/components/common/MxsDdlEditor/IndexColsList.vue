<template>
    <mxs-virtual-scroll-tbl
        :key="keyId"
        :headers="headers"
        :rows="rows"
        :itemHeight="32"
        :maxHeight="dim.height"
        :boundingWidth="dim.width"
        showSelect
        :style="{ width: `${dim.width}px` }"
        :selectedItems.sync="selectedItems"
        :showTotalNumber="false"
        @row-click="onRowClick"
    >
        <template v-slot:[KEY_COL_EDITOR_ATTRS.COL_ORDER]="{ data: { rowData } }">
            {{ getColOrder(rowData) }}
        </template>
        <template
            v-slot:[KEY_COL_EDITOR_ATTRS.ORDER_BY]="{ data: { cell, rowIdx, colIdx, rowData } }"
        >
            <v-select
                :value="cell"
                class="vuetify-input--override v-select--mariadb error--text__bottom error--text__bottom--no-margin"
                :menu-props="{
                    contentClass: 'v-select--menu-mariadb',
                    bottom: true,
                    offsetY: true,
                }"
                :items="Object.values(COL_ORDER_BY)"
                outlined
                dense
                :height="28"
                hide-details
                @input="onChangeInput({ rowIdx, rowData, colIdx, value: $event })"
                @click.stop
            />
        </template>
        <template
            v-slot:[KEY_COL_EDITOR_ATTRS.LENGTH]="{ data: { cell, rowIdx, colIdx, rowData } }"
        >
            <mxs-debounced-field
                :value="cell"
                class="vuetify-input--override error--text__bottom error--text__bottom--no-margin"
                single-line
                outlined
                dense
                :height="28"
                hide-details
                @input="onChangeInput({ rowIdx, rowData, colIdx, value: $event })"
                @click.native.stop
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
 * Change Date: 2027-05-22
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapState } from 'vuex'
import erdHelper from '@wsSrc/utils/erdHelper'

export default {
    name: 'index-cols-list',
    props: {
        value: { type: Object, required: true },
        dim: { type: Object, required: true },
        keyId: { type: String, required: true },
        category: { type: String, required: true },
        tableColNameMap: { type: Object, required: true },
        tableColMap: { type: Array, required: true },
    },
    data() {
        return {
            stagingKeys: {},
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
            let header = { sortable: false, uppercase: true }
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
            return this.$typy(this.stagingKeys[this.category], `${this.keyId}.cols`).safeArray
        },
        indexedColMap() {
            return this.indexedCols.reduce((map, key, i) => {
                map[key.id] = { ...key, index: i }
                return map
            }, {})
        },
        keys: {
            get() {
                return this.value
            },
            set(v) {
                this.$emit('input', v)
            },
        },
    },
    watch: {
        // initialize with fresh data
        keys: {
            deep: true,
            handler(v) {
                if (!this.$helpers.lodash.isEqual(v, this.stagingKeys)) this.init()
            },
        },
        keyId(v, oV) {
            if (v !== oV) this.init()
        },
        // sync changes to keys
        stagingKeys: {
            deep: true,
            handler(v) {
                if (!this.$helpers.lodash.isEqual(this.keys, v)) this.keys = v
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
            this.stagingKeys = this.$helpers.lodash.cloneDeep(this.keys)
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
                    const length = this.$typy(indexedCol, 'length').safeNumber
                    return [id, '', text, type, order, length]
                })
        },
        setInitialSelectedItems() {
            const ids = Object.keys(this.indexedColMap)
            this.selectedItems = this.rows.filter(c => ids.includes(c[this.idxOfId]))
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

            this.stagingKeys = this.$helpers.immutableUpdate(this.stagingKeys, {
                [this.category]: { [this.keyId]: { cols: { $set: cols } } },
            })
        },
    },
}
</script>
