<template>
    <div class="fill-height">
        <div ref="header" class="pb-2 d-flex align-center flex-1">
            <v-spacer />
            <v-tooltip
                v-if="selectedItems.length"
                top
                transition="slide-y-transition"
                content-class="shadow-drop color text-navigation py-1 px-4"
            >
                <template v-slot:activator="{ on }">
                    <v-btn
                        x-small
                        class="mr-2 pa-1 text-capitalize"
                        outlined
                        depressed
                        color="error"
                        v-on="on"
                        @click="deleteSelectedRows(selectedItems)"
                    >
                        {{ $t('drop') }}
                    </v-btn>
                </template>
                <span>{{ $t('dropSelectedCols') }}</span>
            </v-tooltip>
            <v-tooltip
                top
                transition="slide-y-transition"
                content-class="shadow-drop color text-navigation py-1 px-4"
            >
                <template v-slot:activator="{ on }">
                    <v-btn
                        x-small
                        class="pa-1 text-capitalize"
                        outlined
                        depressed
                        color="primary"
                        v-on="on"
                        @click="addNewCol"
                    >
                        {{ $t('add') }}
                    </v-btn>
                </template>
                <span>{{ $t('addNewCol') }}</span>
            </v-tooltip>
        </div>
        <virtual-scroll-table
            :benched="0"
            :headers="headers"
            :rows="rows"
            :itemHeight="40"
            :height="height - headerHeight"
            :boundingWidth="boundingWidth"
            showSelect
            v-on="$listeners"
            @item-selected="selectedItems = $event"
        >
            <template
                v-for="h in headers"
                v-slot:[h.text]="{ data: { rowData, cell, rowIdx, colIdx } }"
            >
                <column-input
                    :key="h.text"
                    :data="{
                        field: h.text,
                        value: cell,
                        rowIdx,
                        colIdx,
                        rowObj: rowDataToObj(rowData),
                    }"
                    :height="30"
                    :defTblCharset="defTblCharset"
                    :defTblCollation="defTblCollation"
                    @on-change="updateCell"
                    @on-change-column_type="onChangeColumnType"
                    @on-change-charset="onChangeCharset"
                />
            </template>
        </virtual-scroll-table>
    </div>
</template>

<script>
import { mapState } from 'vuex'
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-09-20
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import ColumnInput from './ColumnInput.vue'
import { check_charset_support, check_UN_ZF_support, check_AI_support } from './colOptHelpers'
export default {
    name: 'alter-cols-opts',
    components: {
        'column-input': ColumnInput,
    },
    props: {
        value: { type: Object, required: true },
        height: { type: Number, required: true },
        boundingWidth: { type: Number, required: true },
        defTblCharset: { type: String, required: true },
        defTblCollation: { type: String, required: true },
    },
    data() {
        return {
            selectedItems: [],
            headerHeight: 0,
        }
    },
    computed: {
        ...mapState({
            charset_collation_map: state => state.query.charset_collation_map,
        }),
        colsOptsData: {
            get() {
                return this.value
            },
            set(value) {
                this.$emit('input', value)
            },
        },
        headers() {
            return this.$typy(this.colsOptsData, 'fields').safeArray.map(field => {
                let h = {
                    text: field,
                    sortable: false,
                }
                switch (field) {
                    case 'PK':
                    case 'NN':
                    case 'UN':
                    case 'ZF':
                    case 'AI':
                        h.width = 50
                        h.maxWidth = 50
                        break
                }
                return h
            })
        },
        rows() {
            return this.$typy(this.colsOptsData, 'data').safeArray
        },
        idxOfCollation() {
            return this.findHeaderIdx('collation')
        },
        idxOfCharset() {
            return this.findHeaderIdx('charset')
        },
    },
    watch: {
        colsOptsData(v) {
            if (!this.$typy(v).isEmptyObject) this.setHeaderHeight()
        },
    },
    methods: {
        setHeaderHeight() {
            if (this.$refs.header) this.headerHeight = this.$refs.header.clientHeight
        },
        deleteSelectedRows(selectedItems) {
            const { xorWith, isEqual } = this.$help.lodash
            this.colsOptsData = {
                ...this.colsOptsData,
                data: xorWith(this.colsOptsData.data, selectedItems, isEqual),
            }
        },
        addNewCol() {
            let row = []
            this.headers.forEach(h => {
                switch (h.text) {
                    case 'column_name':
                    case 'column_type':
                    case 'comment':
                        row.push('')
                        break
                    case 'PK':
                    case 'NN':
                    case 'UN':
                    case 'ZF':
                    case 'AI':
                        row.push('NO')
                        break
                    default:
                        row.push(null)
                        break
                }
            })
            this.colsOptsData.data.push(row)
        },
        rowDataToObj(rowData) {
            const rows = this.$help.getObjectRows({
                columns: this.$typy(this.colsOptsData, 'fields').safeArray,
                rows: [rowData],
            })
            if (rows.length) return rows[0]
            return []
        },
        findHeaderIdx(headerName) {
            return this.headers.findIndex(h => h.text === headerName)
        },
        /**
         * @param {Object} item - cell data
         */
        updateCell(item) {
            this.colsOptsData = this.$help.immutableUpdate(this.colsOptsData, {
                data: {
                    [item.rowIdx]: {
                        [item.colIdx]: { $set: item.value },
                    },
                },
            })
        },

        /**
         * @param {Object} item - column_type cell data
         */
        onChangeColumnType(item) {
            let colsOptsData
            if (check_charset_support(item.value))
                colsOptsData = this.$help.immutableUpdate(this.colsOptsData, {
                    data: {
                        [item.rowIdx]: {
                            [this.idxOfCharset]: { $set: this.defTblCharset },
                            [this.idxOfCollation]: { $set: this.defTblCollation },
                            [item.colIdx]: { $set: item.value },
                        },
                    },
                })
            else
                colsOptsData = this.$help.immutableUpdate(this.colsOptsData, {
                    data: {
                        [item.rowIdx]: {
                            [this.idxOfCharset]: { $set: null },
                            [this.idxOfCollation]: { $set: null },
                            [item.colIdx]: { $set: item.value },
                        },
                    },
                })

            // update UN, ZF, AI value to NO if chosen type doesn't support
            if (!check_UN_ZF_support(item.value) || !check_AI_support(item.value)) {
                const idxOfUN = this.findHeaderIdx('UN')
                const idxOfZF = this.findHeaderIdx('ZF')
                const idxOfAI = this.findHeaderIdx('AI')
                colsOptsData = this.$help.immutableUpdate(colsOptsData, {
                    data: {
                        [item.rowIdx]: {
                            [idxOfUN]: { $set: 'NO' },
                            [idxOfZF]: { $set: 'NO' },
                            [idxOfAI]: { $set: 'NO' },
                        },
                    },
                })
            }
            this.colsOptsData = colsOptsData
        },

        /**
         * @param {Object} item - charset cell data
         */
        onChangeCharset(item) {
            this.colsOptsData = this.$help.immutableUpdate(this.colsOptsData, {
                data: {
                    [item.rowIdx]: {
                        [this.idxOfCharset]: { $set: item.value },
                        [this.idxOfCollation]: {
                            $set: this.$typy(
                                this.charset_collation_map.get(item.value),
                                'defCollation'
                            ).safeString,
                        },
                    },
                },
            })
        },
    },
}
</script>
