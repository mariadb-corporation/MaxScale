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
            <template v-for="h in headers" v-slot:[h.text]="{ data: { cell, rowIdx, colIdx } }">
                <column-input
                    :key="h.text"
                    :data="{
                        field: h.text,
                        value: cell,
                        rowIdx,
                        colIdx,
                    }"
                    :height="30"
                    :defTblCharset="defTblCharset"
                    :defTblCollation="defTblCollation"
                    :currCharset="$typy(currColCharsetMap, `${rowIdx}`).safeString"
                    :supportCharset="$typy(supportCharsetColMap, `${rowIdx}`).safeBoolean"
                    :supportUnZF="$typy(support_UN_ZF_colMap, `${rowIdx}`).safeBoolean"
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
            typesSupportCharset: [
                'TINYTEXT',
                'TEXT',
                'MEDIUMTEXT',
                'LONGTEXT',
                'CHAR',
                'ENUM',
                'VARCHAR',
                'SET',
            ],
            typesSupport_UN_ZF: [
                'TINYINT',
                'SMALLINT',
                'MEDIUMINT',
                'INT',
                'BIGINT',
                'DECIMAL',
                'FLOAT',
                'DOUBLE',
            ],
            /** supportCharsetColMap is used to handle disable charset/collation inputs
             * a map with rowIdx set as key and boolean value of
             * whether column_type supports charset/collation set as value.
             */
            supportCharsetColMap: {},
            support_UN_ZF_colMap: {},
            /** currColCharsetMap is used to get collations of the charset
             * a map with rowIdx set as key and charset name of the column set as value
             */
            currColCharsetMap: {},
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
        idxOfColumnType() {
            return this.headers.findIndex(h => h.text === 'column_type')
        },
        idxOfCharset() {
            return this.headers.findIndex(h => h.text === 'charset')
        },
    },
    watch: {
        colsOptsData(v) {
            if (!this.$typy(v).isEmptyObject) this.setHeaderHeight()
        },
        rows: {
            deep: true,
            handler(v, oV) {
                if (!this.$help.lodash.isEqual(v, oV)) {
                    this.evaluateInput()
                }
            },
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
        /**
         * This iterates each row to check if column's data type supports
         * the use of charset/collation and store it to `supportCharsetColMap`.
         * It also store charset value of the column to `currColCharsetMap`.
         */
        evaluateInput() {
            let supportCharsetColMap = {},
                currColCharsetMap = {},
                support_UN_ZF_colMap = {}
            this.rows.forEach((row, i) => {
                supportCharsetColMap = {
                    ...supportCharsetColMap,
                    [i]: this.typesSupportCharset.some(v =>
                        row[this.idxOfColumnType].toUpperCase().includes(v)
                    ),
                }
                support_UN_ZF_colMap = {
                    ...support_UN_ZF_colMap,
                    [i]: this.typesSupport_UN_ZF.some(v =>
                        row[this.idxOfColumnType].toUpperCase().includes(v)
                    ),
                }
                currColCharsetMap = {
                    ...currColCharsetMap,
                    [i]: row[this.idxOfCharset],
                }
            })
            this.supportCharsetColMap = supportCharsetColMap
            this.currColCharsetMap = currColCharsetMap
            this.support_UN_ZF_colMap = support_UN_ZF_colMap
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
        updateCharsetCollation({ rowIdx, charset, collation }) {
            const charsetColIdx = this.headers.findIndex(h => h.text === 'charset')
            const collationColIdx = this.headers.findIndex(h => h.text === 'collation')
            this.colsOptsData = this.$help.immutableUpdate(this.colsOptsData, {
                data: {
                    [rowIdx]: {
                        [charsetColIdx]: { $set: charset },
                        [collationColIdx]: { $set: collation },
                    },
                },
            })
        },
        /**
         * @param {Object} item - column_type cell data
         */
        onChangeColumnType(item) {
            this.updateCell(item)
            this.$nextTick(() => {
                if (this.supportCharsetColMap[item.rowIdx])
                    this.updateCharsetCollation({
                        rowIdx: item.rowIdx,
                        charset: this.defTblCharset,
                        collation: this.defTblCollation,
                    })
                else
                    this.updateCharsetCollation({
                        rowIdx: item.rowIdx,
                        charset: null,
                        collation: null,
                    })
                //TODO: Handle AI (AUTO_INCREMENT)
                // update UN, ZF, AI value to NO if chosen type doesn't support
                if (!this.support_UN_ZF_colMap[item.rowIdx]) {
                    const idxOfUN = this.headers.findIndex(h => h.text === 'UN')
                    const idxOfZF = this.headers.findIndex(h => h.text === 'ZF')
                    const idxOfAI = this.headers.findIndex(h => h.text === 'AI')
                    this.colsOptsData = this.$help.immutableUpdate(this.colsOptsData, {
                        data: {
                            [item.rowIdx]: {
                                [idxOfUN]: { $set: 'NO' },
                                [idxOfZF]: { $set: 'NO' },
                                [idxOfAI]: { $set: 'NO' },
                            },
                        },
                    })
                }
            })
        },

        /**
         * @param {Object} item - charset cell data
         */
        onChangeCharset(item) {
            this.updateCharsetCollation({
                rowIdx: item.rowIdx,
                charset: item.value,
                collation: this.$typy(this.charset_collation_map.get(item.value), 'defCollation')
                    .safeString,
            })
        },
    },
}
</script>
