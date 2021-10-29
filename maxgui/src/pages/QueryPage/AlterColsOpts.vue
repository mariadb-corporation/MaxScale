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
                        {{ $t('drop') }} ({{ selectedItems.length }})
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
                        class="mr-2 pa-1 text-capitalize"
                        outlined
                        depressed
                        color="accent-dark"
                        v-on="on"
                        @click="addNewCol"
                    >
                        {{ $t('add') }}
                    </v-btn>
                </template>
                <span>{{ $t('addNewCol') }}</span>
            </v-tooltip>
            <column-list
                v-model="selectedColSpecs"
                returnObject
                :label="$t('alterSpecs')"
                :cols="colSpecs"
                :maxHeight="tableMaxHeight - 20"
            />

            <v-tooltip
                top
                transition="slide-y-transition"
                content-class="shadow-drop color text-navigation py-1 px-4"
            >
                <template v-slot:activator="{ on }">
                    <v-btn
                        x-small
                        class="ml-2 pa-1"
                        outlined
                        depressed
                        color="accent-dark"
                        v-on="on"
                        @click="isVertTable = !isVertTable"
                    >
                        <v-icon
                            size="14"
                            color="accent-dark"
                            :class="{ 'rotate-icon__vert': !isVertTable }"
                        >
                            rotate_90_degrees_ccw
                        </v-icon>
                    </v-btn>
                </template>
                <span>{{ $t(isVertTable ? 'switchToHorizTable' : 'switchToVertTable') }}</span>
            </v-tooltip>
        </div>

        <virtual-scroll-table
            :headers="visHeaders"
            :rows="rows"
            :itemHeight="40"
            :maxHeight="tableMaxHeight"
            :boundingWidth="boundingWidth"
            showSelect
            :isVertTable="isVertTable"
            v-on="$listeners"
            @item-selected="selectedItems = $event"
        >
            <template
                v-for="(h, colIdx) in visHeaders"
                v-slot:[h.text]="{ data: { rowData, cell, rowIdx } }"
            >
                <div :key="h.text" class="fill-height d-flex align-center">
                    <column-input
                        :ref="`columnInput-row${rowIdx}-col-${colIdx}`"
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
                        :dataTypes="dataTypes"
                        @on-input="onCellInput"
                        @on-input-column_type="onInputColumnType"
                        @on-input-PK="onInputPK"
                        @on-input-NN="onInputNN"
                        @on-input-AI="onInputAI"
                        @on-input-generated="onInputGenerated"
                        @on-input-charset="onInputCharset"
                    />
                </div>
            </template>
            <!-- Add :key so that truncate-string rerender to evaluate truncation and fallback text-truncate class  -->
            <template v-slot:header-column_name="{ data: { maxWidth } }">
                <truncate-string
                    :key="maxWidth"
                    class="text-truncate"
                    text="Column Name"
                    :maxWidth="maxWidth"
                />
            </template>
            <template v-slot:header-column_type="{ data: { maxWidth } }">
                <truncate-string
                    :key="maxWidth"
                    class="text-truncate"
                    text="Column Type"
                    :maxWidth="maxWidth"
                />
            </template>
            <template
                v-for="(value, key) in abbreviatedHeaders"
                v-slot:[abbrHeaderSlotName(key)]="{ data: { header, maxWidth } }"
            >
                <v-tooltip
                    :key="key"
                    top
                    transition="slide-y-transition"
                    content-class="shadow-drop color text-navigation py-1 px-4"
                    :disabled="isVertTable"
                >
                    <template v-slot:activator="{ on }">
                        <div
                            class="d-inline-block text-truncate"
                            :style="{ maxWidth: `${maxWidth}px` }"
                            v-on="on"
                        >
                            {{ isVertTable ? value : header.text }}
                        </div>
                    </template>
                    <span>{{ value }}</span>
                </v-tooltip>
            </template>
        </virtual-scroll-table>
    </div>
</template>

<script>
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
import { mapState } from 'vuex'
import column_types from './column_types'
import ColumnInput from './ColumnInput.vue'
import ColumnList from './ColumnList.vue'
import { check_charset_support, check_UN_ZF_support, check_AI_support } from './colOptHelpers'
export default {
    name: 'alter-cols-opts',
    components: {
        'column-input': ColumnInput,
        'column-list': ColumnList,
    },
    props: {
        value: { type: Object, required: true },
        initialData: { type: Object, required: true },
        height: { type: Number, required: true },
        boundingWidth: { type: Number, required: true },
        defTblCharset: { type: String, required: true },
        defTblCollation: { type: String, required: true },
    },
    data() {
        return {
            selectedItems: [],
            headerHeight: 0,
            isVertTable: false,
            selectedColSpecs: [],
        }
    },
    computed: {
        ...mapState({
            charset_collation_map: state => state.query.charset_collation_map,
        }),
        tableMaxHeight() {
            return this.height - this.headerHeight
        },
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
                    capitalize: true,
                }
                switch (field) {
                    case 'PK':
                    case 'NN':
                    case 'UN':
                    case 'UQ':
                    case 'ZF':
                    case 'AI':
                        h.minWidth = 50
                        h.maxWidth = 50
                        h.resizable = false
                        break
                    case 'generated':
                        h.width = 144
                        h.minWidth = 126
                        break
                    case 'id':
                        h.hidden = true
                        break
                }
                return h
            })
        },
        colSpecs() {
            return this.headers.filter(h => !h.hidden)
        },
        visHeaders() {
            return this.headers.map(h => {
                if (!this.selectedColSpecs.find(col => col.text === h.text))
                    return { ...h, hidden: true }
                return h
            })
        },
        abbreviatedHeaders() {
            return {
                PK: 'PRIMARY KEY',
                NN: 'NOT NULL',
                UN: 'UNSIGNED',
                UQ: 'UNIQUE INDEX',
                ZF: 'ZEROFILL',
                AI: 'AUTO_INCREMENT',
                generated: 'Generated column',
            }
        },
        rows() {
            return this.$typy(this.colsOptsData, 'data').safeArray
        },
        dataTypes() {
            let items = []
            column_types.forEach(item => {
                // place header first, then all its types and add a divider
                items = [...items, { header: item.header }, ...item.types, { divider: true }]
            })
            return items
        },
        idxOfId() {
            return this.findHeaderIdx('id')
        },
        idxOfCollation() {
            return this.findHeaderIdx('collation')
        },
        idxOfCharset() {
            return this.findHeaderIdx('charset')
        },
        idxOfAI() {
            return this.findHeaderIdx('AI')
        },
        idxOfGenerated() {
            return this.findHeaderIdx('generated')
        },
        idxOfUN() {
            return this.findHeaderIdx('UN')
        },
        idxOfNN() {
            return this.findHeaderIdx('NN')
        },
        idxOfUQ() {
            return this.findHeaderIdx('UQ')
        },
        idxOfDefAndExp() {
            return this.findHeaderIdx('default/expression')
        },
        hasAI() {
            let count = 0
            this.rows.forEach(row => {
                if (row[this.idxOfAI] === 'AUTO_INCREMENT') count++
            })
            return count === 1
        },
        /**
         * a unique key of each table being altered.
         * This key is used to handle show column options in the case when
         * user alters a table then alter another table in the same worksheet
         */
        initialDataFirstCellId() {
            return this.$typy(this.initialData.data, `[0]['${this.idxOfId}']`).safeString
        },
    },
    watch: {
        colsOptsData(v) {
            if (!this.$typy(v).isEmptyObject) this.setHeaderHeight()
        },
    },
    activated() {
        this.addInitialDataFirstCellIdWatcher()
        this.addWidthBreakpointWatcher()
    },
    deactivated() {
        this.rmInitialDataFirstCellIdWatcher()
        this.rmWidthBreakpointWatcherr()
    },
    methods: {
        addInitialDataFirstCellIdWatcher() {
            this.rmInitialDataFirstCellIdWatcher = this.$watch('initialDataFirstCellId', v => {
                if (v) this.handleShowColSpecs()
            })
        },
        addWidthBreakpointWatcher() {
            this.rmWidthBreakpointWatcherr = this.$watch('$vuetify.breakpoint.width', () =>
                this.handleShowColSpecs()
            )
        },
        handleShowColSpecs() {
            const colSpecs = this.$help.lodash.cloneDeep(this.colSpecs)
            if (this.$vuetify.breakpoint.width >= 1680) this.selectedColSpecs = colSpecs
            else {
                const hiddenSpecs = ['charset', 'collation', 'comment']
                this.selectedColSpecs = colSpecs.filter(col => !hiddenSpecs.includes(col.text))
            }
        },
        abbrHeaderSlotName(h) {
            if (this.isVertTable) return `vertical-header-${h}`
            return `header-${h}`
        },
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
                    case 'id':
                        row.push(this.$help.uuidv1())
                        break
                    case 'NN':
                        row.push('NULL')
                        break
                    case 'PK':
                        row.push('NO')
                        break
                    default:
                        row.push('')
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
        onCellInput(item) {
            this.colsOptsData = this.$help.immutableUpdate(this.colsOptsData, {
                data: {
                    [item.rowIdx]: {
                        [item.colIdx]: { $set: item.value },
                    },
                },
            })
        },
        /**
         * This patches charset and collation at provided rowIdx
         * @param {Object} payload.colsOptsData - current colsOptsData
         * @param {Number} payload.rowIdx - row to be updated
         * @param {String} payload.charset - charset to set.
         * @returns {Object} - returns new colsOptsData
         */
        patchCharsetCollation({ colsOptsData, rowIdx, charset }) {
            return this.$help.immutableUpdate(colsOptsData, {
                data: {
                    [rowIdx]: {
                        [this.idxOfCharset]: { $set: charset },
                        [this.idxOfCollation]: {
                            $set: this.$typy(
                                this.charset_collation_map.get(charset),
                                'defCollation'
                            ).safeString,
                        },
                    },
                },
            })
        },
        /**
         * This handles auto set charset/collation to use utf8
         * @param {Object} payload.colsOptsData - current colsOptsData
         * @param {Object} payload.item - cell item
         * @returns {Object} - returns new colsOptsData
         */
        handleNationalType({ colsOptsData, item }) {
            if (item.value.includes('NATIONAL'))
                return this.patchCharsetCollation({
                    colsOptsData,
                    rowIdx: item.rowIdx,
                    charset: 'utf8',
                })
            return colsOptsData
        },
        /**
         * This handles auto uncheck UN, ZF, AI checkboxes if chosen type doesn't support
         * @param {Object} payload.colsOptsData - current colsOptsData
         * @param {Object} payload.item - cell item
         * @returns {Object} - returns new colsOptsData
         */
        handleUncheck_UN_ZF_AI({ colsOptsData, item }) {
            if (!check_UN_ZF_support(item.value) || !check_AI_support(item.value)) {
                const idxOfZF = this.findHeaderIdx('ZF')
                return this.$help.immutableUpdate(colsOptsData, {
                    data: {
                        [item.rowIdx]: {
                            [this.idxOfUN]: { $set: '' },
                            [idxOfZF]: { $set: '' },
                            [this.idxOfAI]: { $set: '' },
                        },
                    },
                })
            }
            return colsOptsData
        },
        /**
         * This handles set default charset/collation
         * @param {Object} payload.colsOptsData - current colsOptsData
         * @param {Object} payload.item - cell item
         * @returns {Object} - returns new colsOptsData
         */
        handleSetDefCharset({ colsOptsData, item }) {
            let charset = null,
                collation = null
            if (check_charset_support(item.value)) {
                charset = this.defTblCharset
                collation = this.defTblCollation
            }
            return this.$help.immutableUpdate(colsOptsData, {
                data: {
                    [item.rowIdx]: {
                        [this.idxOfCharset]: { $set: charset },
                        [this.idxOfCollation]: { $set: collation },
                    },
                },
            })
        },
        /**
         * This handles SERIAL type
         * @param {Object} payload.colsOptsData - current colsOptsData
         * @param {Object} payload.item - cell item
         * @returns {Object} - returns new colsOptsData
         */
        handleSerialType({ colsOptsData, item }) {
            if (item.value === 'SERIAL') {
                const columnInput = this.$refs[
                    `columnInput-row${item.rowIdx}-col-${this.idxOfUQ}`
                ][0]
                return this.$help.immutableUpdate(colsOptsData, {
                    data: {
                        [item.rowIdx]: {
                            [this.idxOfUN]: { $set: 'UNSIGNED' },
                            [this.idxOfNN]: { $set: 'NOT NULL' },
                            [this.idxOfAI]: { $set: 'AUTO_INCREMENT' },
                            [this.idxOfUQ]: {
                                $set: columnInput.uniqueIdxName,
                            },
                        },
                    },
                })
            }
            return colsOptsData
        },
        /**
         * @param {Object} item - column_type cell data
         */
        onInputColumnType(item) {
            // first update column_type cell
            let colsOptsData = this.$help.immutableUpdate(this.colsOptsData, {
                data: {
                    [item.rowIdx]: {
                        [item.colIdx]: { $set: item.value },
                    },
                },
            })
            colsOptsData = this.handleSetDefCharset({ colsOptsData, item })
            colsOptsData = this.handleUncheck_UN_ZF_AI({ colsOptsData, item })
            colsOptsData = this.handleNationalType({ colsOptsData, item })
            colsOptsData = this.handleSerialType({ colsOptsData, item })
            this.colsOptsData = colsOptsData
        },
        /**
         * This unchecks the other auto_increment as there
         * can be one table column has this.
         * @param {Object} payload.colsOptsData - current colsOptsData
         * @param {Number} payload.rowIdx - rowIdx to be excluded
         * @returns {Object} - returns new colsOptsData
         */
        uncheckOtherAI({ colsOptsData, rowIdx }) {
            let idx
            for (const [i, row] of this.rows.entries())
                if (row[this.idxOfAI] === 'AUTO_INCREMENT' && i !== rowIdx) {
                    idx = i
                    break
                }

            if (idx >= 0)
                return (colsOptsData = this.$help.immutableUpdate(colsOptsData, {
                    data: {
                        [idx]: {
                            [this.idxOfAI]: { $set: '' },
                        },
                    },
                }))
            return colsOptsData
        },

        /**
         * This updates NN cell and `default` cell.
         * @param {Object} payload.colsOptsData - current colsOptsData
         * @param {Number} payload.rowIdx - rowIdx to be updated
         * @param {String} payload.valOfNN - value of NN
         * @param {String} payload.valueOfDefault - value of default cell
         * @returns {Object} - returns new colsOptsData
         */
        notNullSideEffect({ colsOptsData, rowIdx, valOfNN, valueOfDefault = null }) {
            let defaultVal = this.$typy(colsOptsData, `data['${rowIdx}']['${this.idxOfDefAndExp}']`)
                .safeString
            if (defaultVal === 'NULL' && valOfNN === 'NOT NULL') defaultVal = ''
            if (valueOfDefault !== null) defaultVal = valueOfDefault
            return this.$help.immutableUpdate(colsOptsData, {
                data: {
                    [rowIdx]: {
                        [this.idxOfNN]: { $set: valOfNN },
                        [this.idxOfDefAndExp]: { $set: defaultVal },
                    },
                },
            })
        },
        /**
         * @param {Object} item - AI cell data
         */
        onInputAI(item) {
            let colsOptsData = this.colsOptsData
            if (this.hasAI)
                colsOptsData = this.uncheckOtherAI({ colsOptsData, rowIdx: item.rowIdx })
            // update AI and generated cells
            colsOptsData = this.$help.immutableUpdate(colsOptsData, {
                data: {
                    [item.rowIdx]: {
                        [item.colIdx]: { $set: item.value },
                        [this.idxOfGenerated]: { $set: '(none)' },
                    },
                },
            })
            this.colsOptsData = this.notNullSideEffect({
                colsOptsData,
                rowIdx: item.rowIdx,
                valOfNN: 'NOT NULL',
                // set to empty string when AI value is AUTO_INCREMENT
                valueOfDefault: item.value ? '' : null,
            })
        },
        /**
         * @param {Object} item - G cell data
         */
        onInputGenerated(item) {
            let colsOptsData = this.colsOptsData
            let defaultVal = ''
            if (item.value === '(none)') {
                const nnVal = this.$typy(colsOptsData, `data['${item.rowIdx}']['${this.idxOfNN}']`)
                    .safeString
                if (nnVal === 'NULL') defaultVal = 'NULL'
                else if (nnVal === 'NOT NULL') defaultVal = ''
            }
            // use initial expression value or empty string
            else {
                defaultVal = this.$typy(
                    this.initialData.data,
                    `['${item.rowIdx}']['${this.idxOfDefAndExp}']`
                ).safeString
            }

            // update G and its dependencies cell value
            colsOptsData = this.$help.immutableUpdate(colsOptsData, {
                data: {
                    [item.rowIdx]: {
                        [item.colIdx]: { $set: item.value },
                        [this.idxOfAI]: { $set: '' },
                        [this.idxOfNN]: { $set: '' },
                        [this.idxOfDefAndExp]: { $set: defaultVal },
                    },
                },
            })
            this.colsOptsData = colsOptsData
        },
        /**
         * @param {Object} item - charset cell data
         */
        onInputCharset(item) {
            this.colsOptsData = this.patchCharsetCollation({
                colsOptsData: this.colsOptsData,
                rowIdx: item.rowIdx,
                charset: item.value,
            })
        },
        /**
         * @param {Object} item - PK cell data
         */
        onInputPK(item) {
            // update PK and UQ value
            let colsOptsData = this.$help.immutableUpdate(this.colsOptsData, {
                data: {
                    [item.rowIdx]: {
                        [item.colIdx]: { $set: item.value },
                        [this.idxOfUQ]: { $set: '' },
                    },
                },
            })
            this.colsOptsData = this.notNullSideEffect({
                colsOptsData,
                rowIdx: item.rowIdx,
                valOfNN: 'NOT NULL',
            })
        },
        /**
         * @param {Object} item - NN cell data
         */
        onInputNN(item) {
            this.colsOptsData = this.notNullSideEffect({
                colsOptsData: this.colsOptsData,
                rowIdx: item.rowIdx,
                valOfNN: item.value,
            })
        },
    },
}
</script>
